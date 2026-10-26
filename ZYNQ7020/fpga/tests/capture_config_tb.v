`timescale 1ns/1ps
module capture_config_tb;
    reg clk=0,passed=0;
    reg [31:0] control=0;
    wire enable;
    wire [2:0] clock_shift;
    wire [31:0] rate,divider,payload;
    integer index,expected_rate,scale;
    capture_config dut(clk,control,enable,rate,divider,payload,clock_shift,);
    always #5 clk=~clk;
    initial begin
        expected_rate=200000000;
        for(index=0;index<20;index=index+1)begin
            scale=index%18;
            @(negedge clk);control=(index<<1)|(scale<<6);
            repeat(5)@(negedge clk);
            if(rate!==expected_rate || payload!==(8<<scale) || enable!==0)
                $fatal(1,"configuration %d rate %d payload %d",index,rate,payload);
            if(divider!==(index==0?0:100000000/expected_rate))$fatal(1,"divider");
            control=control|1;repeat(5)@(negedge clk);
            if(!enable || rate!==expected_rate)$fatal(1,"enable changed configuration");
            case(index%3)0:expected_rate=expected_rate/2;1:expected_rate=expected_rate/2;2:expected_rate=expected_rate*2/5;endcase
        end
        for(index=6;index>=0;index=index-1)begin
            control=(index<<11)|(17<<6);repeat(5)@(negedge clk);
            if(clock_shift!==index || rate!==(200000000>>index) || divider!==0)
                $fatal(1,"physical encode divider %d",index);
        end
        passed=1;$display("PASS: 20 capture rates and 7 physical encode clocks");$finish;
    end
endmodule
