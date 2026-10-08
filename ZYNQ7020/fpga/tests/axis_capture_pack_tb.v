`timescale 1ns/1ps
module axis_capture_pack_tb;
    reg clk=0,resetn=0,valid=0,ready=0;
    reg [31:0] data=0;
    wire input_ready,output_valid,last;
    wire [63:0] output_data;
    wire [7:0] keep;
    integer sent=0,received=0,cycles=0;
    reg held=0;
    reg [63:0] held_data;
    reg held_last;
    axis_capture_pack #(.BLOCK_BEATS(4)) dut(clk,resetn,data,valid,input_ready,output_data,keep,output_valid,ready,last);
    always #5 clk=~clk;
    initial begin repeat(3)@(negedge clk);resetn=1;end
    always @(negedge clk) begin
        if(resetn)begin
            cycles=cycles+1;ready=cycles%7<3;valid=sent<64;data=sent;
            if(cycles>1000)$fatal(1,"timeout");
        end
    end
    always @(posedge clk)if(resetn)begin
        if(held && (!output_valid || output_data!==held_data || last!==held_last))$fatal(1,"unstable stalled output");
        held=output_valid&&!ready;held_data=output_data;held_last=last;
        if(valid&&input_ready)sent=sent+1;
        if(output_valid&&ready)begin
            if(output_data!=={32'(received*2+1),32'(received*2)}||keep!==8'hff||last!==(received%4==3))$fatal(1,"order or TLAST");
            received=received+1;
            if(received==32)begin $display("PASS: 32 beats, backpressure, sample order, TLAST");$finish;end
        end
    end
endmodule
