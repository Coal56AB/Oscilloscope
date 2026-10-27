`timescale 1ns/1ps
module capture_dsp_config_tb;
    reg clk=0,sample_clk=0,resetn=0,sample_resetn=0,sample_clock_enabled=1,passed=0;
    reg [7:0] awaddr,araddr;
    reg awvalid=0,wvalid=0,bready=0,arvalid=0,rready=0;
    reg [31:0] wdata;
    reg [3:0] wstrb;
    wire awready,wready,bvalid,arready,rvalid;
    wire [1:0] bresp,rresp;
    wire [31:0] rdata;
    wire [287:0] configuration;
    wire update;
    reg [287:0] expected=0,previous=0;
    reg [31:0] value;
    integer updates=0,i;
    capture_dsp_config dut(clk,resetn,awaddr,3'd0,awvalid,awready,wdata,wstrb,wvalid,wready,bresp,bvalid,bready,
        araddr,3'd0,arvalid,arready,rdata,rresp,rvalid,rready,sample_clk,sample_resetn,configuration,update);
    always #5 clk=~clk;
    always #7 if(sample_clock_enabled)sample_clk=~sample_clk;
    task send_address(input [7:0] address);
        begin
            @(negedge clk);awaddr=address;awvalid=1;
            do @(posedge clk);while(!awready);
            @(negedge clk);awvalid=0;
        end
    endtask
    task send_data(input [31:0] data,input [3:0] strobe);
        begin
            @(negedge clk);wdata=data;wstrb=strobe;wvalid=1;
            do @(posedge clk);while(!wready);
            @(negedge clk);wvalid=0;
        end
    endtask
    task write(input [7:0] address,input [31:0] data,input [3:0] strobe,input integer order,input [1:0] response);
        begin
            if(order==0)begin send_address(address);repeat(3)@(negedge clk);send_data(data,strobe);end
            else if(order==1)begin send_data(data,strobe);repeat(3)@(negedge clk);send_address(address);end
            else fork send_address(address);send_data(data,strobe);join
            wait(bvalid);repeat(4)begin @(negedge clk);if(!bvalid || bresp!==response)$fatal(1,"unstable write response");end
            bready=1;@(negedge clk);bready=0;
        end
    endtask
    task read(input [7:0] address,input [1:0] response,output [31:0] data);
        begin
            @(negedge clk);araddr=address;arvalid=1;
            do @(posedge clk);while(!arready);
            @(negedge clk);arvalid=0;wait(rvalid);data=rdata;
            repeat(4)begin @(negedge clk);if(!rvalid || rdata!==data || rresp!==response)$fatal(1,"unstable read response");end
            rready=1;@(negedge clk);rready=0;
        end
    endtask
    always @(negedge sample_clk)if(sample_resetn)begin
        if(update)begin
            updates=updates+1;
            if(configuration!==expected)$fatal(1,"partial configuration update");
        end else if(configuration!==previous)$fatal(1,"configuration changed without update");
        previous=configuration;
    end
    initial begin
        repeat(4)@(negedge clk);resetn=1;sample_resetn=1;
        read(0,0,value);if(value!==32'h31505344)$fatal(1,"bad ID");
        for(i=0;i<8;i=i+1)begin
            write(16+4*i,32'h12000000+i,15,i%3,0);expected[i*32+:32]=32'h12000000+i;
        end
        write(20,32'h00ab00cd,5,2,0);expected[32+:32]=32'h12ab00cd;
        read(20,0,value);if(value!==expected[32+:32])$fatal(1,"byte strobes");
        @(negedge sample_clk);sample_clock_enabled=0;
        expected[287:256]=1;write(8,1,15,2,0);
        write(16,32'h34000000,15,0,0);write(8,2,15,1,2);
        read(4,0,value);if(value!==1)$fatal(1,"missing busy");
        sample_clock_enabled=1;wait(updates==1);repeat(4)@(negedge clk);
        read(4,0,value);if(value!==0)$fatal(1,"busy after ACK");
        read(12,0,value);if(value!==1)$fatal(1,"arm ID");
        expected[0+:32]=32'h34000000;
        for(i=1;i<8;i=i+1)begin write(16+4*i,32'h34000000+i,15,i%3,0);expected[i*32+:32]=32'h34000000+i;end
        expected[287:256]=2;write(8,2,15,0,0);wait(updates==2);
        write(0,0,15,2,2);write(18,0,15,1,2);write(8,3,3,0,2);read(240,2,value);
        repeat(10)@(negedge clk);if(updates!=2)$fatal(1,"unexpected update");
        passed=1;$display("PASS: atomic DSP configuration CDC, independent AW/W, byte strobes, busy rejection and backpressure");$finish;
    end
    initial begin #100000;$fatal(1,"timeout");end
endmodule
