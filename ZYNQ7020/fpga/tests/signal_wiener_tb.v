`timescale 1ns/1ps
module signal_wiener_tb;
    reg clk=0,resetn=0,valid=0,passed=0;
    reg [7:0] center;
    reg [12:0] sum;
    reg [22:0] variance;
    reg [31:0] noise_power;
    wire result_valid;
    wire [23:0] result;
    signal_wiener dut(clk,resetn,valid,center,sum,variance,noise_power,result_valid,result);
    always #5 clk=~clk;
    real expected[0:32767];
    real mean,power,v,wanted,error,maximum_error=0;
    integer sent=0,received=0,cycles=0,phase,j,x,s,sq,bias,amplitude;
    reg [31:0] random_state=32'h5a716c9d;
    function [31:0] step(input [31:0] value);
        begin step={value[30:0],value[31]^value[21]^value[1]^value[0]};end
    endfunction
    initial begin
        repeat(3)@(negedge clk);resetn=1;
        for(phase=0;phase<16000;phase=phase+1)begin
            @(negedge clk);random_state=step(random_state);valid=phase%19<15;
            if(valid)begin
                s=0;sq=0;bias=random_state[7:0];amplitude=random_state[12:8];
                for(j=0;j<17;j=j+1)begin
                    random_state=step(random_state);
                    x=bias+(random_state[0]?amplitude:-amplitude);
                    if(phase%11==0 && j==8)x=random_state[15:8];
                    if(phase%13==0)x=bias;
                    if(x<0)x=0;if(x>255)x=255;
                    if(j==8)center=x;s=s+x;sq=sq+x*x;
                end
                sum=s;variance=17*sq-s*s;
                case(phase%7)
                    0:noise_power=0;
                    1:noise_power=32'hffffffff;
                    2:noise_power=32'd2598502; // 39.650 code^2
                    3:noise_power=(variance*64'd65536)/289;
                    4:noise_power=(variance*64'd65536)/(9*289);
                    default:noise_power={16'd0,random_state[15:0]}*32'd97;
                endcase
                power=noise_power/65536.0;mean=s/17.0;v=variance/289.0;
                wanted=center;
                if(power>0 && v<9*power)wanted=v>power?mean+(1-power/v)*(center-mean):mean;
                expected[sent]=wanted;sent=sent+1;
            end
        end
        @(negedge clk);valid=0;wait(received==sent);
        // Reset cancels pending values, including a full-rate pipeline.
        @(negedge clk);valid=1;repeat(12)@(negedge clk);
        resetn=0;valid=0;repeat(3)@(negedge clk);resetn=1;
        repeat(50)begin @(negedge clk);if(result_valid)$fatal(1,"stale result after reset");end
        passed=1;$display("PASS: %0d Wiener samples, max error %0.9f ADC code",received,maximum_error);$finish;
    end
    always @(posedge clk)begin
        cycles=cycles+1;if(cycles>20000)$fatal(1,"timeout");
        if(resetn && result_valid)begin
            if(received>=sent)$fatal(1,"unexpected result");
            error=result/65536.0-expected[received];if(error<0)error=-error;
            if(error>maximum_error)maximum_error=error;
            if(error>0.00004)$fatal(1,"sample %0d got %0.9f expected %0.9f error %0.9f",received,result/65536.0,expected[received],error);
            received=received+1;
        end
    end
endmodule
