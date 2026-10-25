`timescale 1ns/1ps
module ad9288_settle_case #(parameter real RETURN_DELAY=6.0)(output reg passed=0);
    reg encode_clk=0,sample_clk=0,resetn=0,enable=0,checking=0;
    reg [2:0] clock_shift=6;
    reg [7:0] a=0,b=0,previous=0;
    wire clk_a,clk_b,valid,stream_resetn,overflow;
    wire [31:0] data;
    integer generation=0,shift,words=0;
    reg have_previous=0;
    ad9288_input dut(encode_clk,sample_clk,resetn,enable,a,b,a+8'd64,b+8'd64,
        clk_a,clk_b,data,valid,1'b1,stream_resetn,overflow,clock_shift);
    always #5 encode_clk=~encode_clk;
    initial begin #4.125;forever #5 sample_clk=~sample_clk;end
    // A/B encode one generation at opposite half-periods. Include the
    // forwarding/output/return delay, rather than only ADC tPD in isolation.
    always @(posedge clk_a)begin generation=generation+1;a<=#RETURN_DELAY generation;end
    always @(posedge clk_b)b<=#RETURN_DELAY generation;
    always @(posedge sample_clk)if(valid && checking)begin
        if(data[15:8]!==data[7:0]+8'd64 || data[31:24]!==data[23:16]+8'd64)
            $fatal(1,"channel packing delay=%f shift=%d",RETURN_DELAY,clock_shift);
        if(data[7:0]!==data[23:16])
            $fatal(1,"A/B generation mismatch delay=%f shift=%d A=%d B=%d",
                RETURN_DELAY,clock_shift,data[7:0],data[23:16]);
        if(have_previous && data[7:0]!==previous+8'd1)
            $fatal(1,"repeated/missing generation delay=%f shift=%d",RETURN_DELAY,clock_shift);
        previous=data[7:0];have_previous=1;words=words+1;
    end
    initial begin
        #100;resetn=1;
        for(shift=6;shift>=0;shift=shift-1)begin
            checking=0;enable=0;clock_shift=shift;#40000;
            enable=1;#40000;have_previous=0;words=0;checking=1;#40000;
            if(overflow || words<(4000>>shift)-1 || words>(4000>>shift)+1)
                $fatal(1,"incorrect frame rate delay=%f shift=%d words=%d",RETURN_DELAY,shift,words);
        end
        checking=0;passed=1;
    end
endmodule

module ad9288_settle_tb;
    wire passed_short,passed_return;
    reg passed=0;
    ad9288_settle_case #(.RETURN_DELAY(6.0)) short_path(passed_short);
    ad9288_settle_case #(.RETURN_DELAY(13.0)) return_path(passed_return);
    initial begin
        wait(passed_short && passed_return);passed=1;
        $display("PASS: A/B generations at 1.5625..100 MHz with 6/13 ns return paths");$finish;
    end
endmodule
