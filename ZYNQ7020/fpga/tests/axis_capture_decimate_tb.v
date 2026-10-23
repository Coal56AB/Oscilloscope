`timescale 1ns/1ps
module axis_capture_decimate_tb;
    reg clk=0, resetn=0, valid=0, ready=0;
    reg [31:0] divider=0, data=0;
    wire take, out_valid;
    wire [31:0] out_data;
    reg passed=0;
    integer sent=0, received=0, cycle=0, epoch=0, target=0;
    integer lo, hi;
    reg held=0;
    reg [31:0] held_data;
    axis_capture_decimate dut(clk,resetn,divider,data,valid,take,out_data,out_valid,ready);
    always #5 clk=~clk;
    initial begin
        for(epoch=0;epoch<4;epoch=epoch+1) begin
            @(negedge clk); resetn=0;valid=0;ready=0;
            repeat(3) @(negedge clk);
            case(epoch) 0:divider=0;1:divider=1;2:divider=5;3:divider=100;endcase
            sent=0;received=0;held=0;cycle=0;
            target=divider==0?16:32*divider;
            resetn=1;
            wait(received==16);
        end
        passed=1;$display("PASS: decimation 1/2/10/200, channel order and backpressure");$finish;
    end
    always @(negedge clk) if(resetn) begin
        cycle=cycle+1;valid=sent<target && cycle%7!=0;ready=cycle%11<7;
        data={16'(sent*2+1),16'(sent*2)};
        if(cycle>20000)$fatal(1,"timeout");
    end
    always @(posedge clk) if(resetn) begin
        if(held && (!out_valid || out_data!==held_data))$fatal(1,"changed during stall");
        held=out_valid&&!ready;held_data=out_data;
        if(valid&&take)sent=sent+1;
        if(out_valid&&ready)begin
            lo=divider==0?received*2:received*4*divider;
            hi=divider==0?lo+1:lo+2*divider;
            if(out_data!=={16'(hi),16'(lo)})$fatal(1,"divider %d got %h expected %d,%d",divider,out_data,hi,lo);
            received=received+1;
        end
    end
endmodule
