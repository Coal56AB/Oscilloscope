`timescale 1ns/1ps
module axis_capture_decimate_tb;
    reg clk=0,resetn=0,valid=0,ready=0,passed=0;
    reg [31:0] divider=0,data=0;
    wire take,out_valid,peaks;
    wire [31:0] out_data;
    reg [31:0] expected[0:1023];
    reg [31:0] held_data;
    reg held=0;
    integer sent,received,cycle,epoch,target,written,pos;
    integer low0,low1,high0,high1,c0,c1;
    reg [15:0] first_pair,second_pair;
    axis_capture_decimate dut(clk,resetn,divider,data,valid,take,out_data,out_valid,ready,peaks);
    always #5 clk=~clk;
    initial begin
        for(epoch=0;epoch<5;epoch=epoch+1)begin
            @(negedge clk);resetn=0;valid=0;ready=0;
            repeat(3)@(negedge clk);
            case(epoch)0:divider=0;1:divider=1;2:divider=5;3:divider=100;4:divider=1;endcase
            sent=0;received=0;written=0;cycle=0;pos=0;held=0;
            target=divider==0?16:16*divider;
            resetn=1;
            wait(received==16);
        end
        passed=1;$display("PASS: raw/peak decimation, both ADC branches, all extrema, stalls and full-throughput /2");$finish;
    end
    always @(negedge clk)if(resetn)begin
        cycle=cycle+1;
        valid=sent<target && (epoch==4 || cycle%7!=0);
        ready=epoch==4 || cycle%11<7;
        data={8'(sent*7+21),8'(sent*3+199),8'(sent*5+11),8'(sent*2)};
        if(cycle>30000)$fatal(1,"timeout");
    end
    always @(posedge clk)if(resetn)begin
        if(peaks!==(divider!=0))$fatal(1,"format flag");
        if(held && (!out_valid || out_data!==held_data))$fatal(1,"unstable stalled output");
        held=out_valid&&!ready;held_data=out_data;
        if(epoch==4 && valid && !take)$fatal(1,"ADC stalled at continuous /2");
        if(valid&&take)begin
            if(divider==0)begin expected[written]=data;written=written+1;end
            else begin
                if(pos==0)begin low0=255;low1=255;high0=0;high1=0;first_pair=data[15:0];end
                if(pos==divider)second_pair=data[15:0];
                c0=data[7:0];c1=data[15:8];
                if(c0<low0)low0=c0;if(c0>high0)high0=c0;
                if(c1<low1)low1=c1;if(c1>high1)high1=c1;
                c0=data[23:16];c1=data[31:24];
                if(c0<low0)low0=c0;if(c0>high0)high0=c0;
                if(c1<low1)low1=c1;if(c1>high1)high1=c1;
                if(pos==2*divider-1)begin
                    expected[written]={second_pair,first_pair};
                    expected[written+1]={8'(high1),8'(high0),8'(low1),8'(low0)};
                    written=written+2;pos=0;
                end else pos=pos+1;
            end
            sent=sent+1;
        end
        if(out_valid&&ready)begin
            if(received>=written || out_data!==expected[received])
                $fatal(1,"divider %d word %d got %h expected %h",divider,received,out_data,expected[received]);
            received=received+1;
        end
    end
endmodule
