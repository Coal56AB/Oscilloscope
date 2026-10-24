`timescale 1ns/1ps
module ad9288_clock_tb;
    reg encode_clk=0,sample_clk=0,resetn=0,enable=0,passed=0;
    reg [2:0] clock_shift=6;
    reg [7:0] a=0,b=0;
    wire clk_a,clk_b,valid,stream_resetn,overflow;
    wire [31:0] data;
    integer edges_a=0,edges_b=0,words=0,shift,begin_a,begin_b,begin_words;
    reg check_words=0,have_previous=0;
    reg [31:0] previous;
    ad9288_input dut(encode_clk,sample_clk,resetn,enable,a,b,a+8'd64,b+8'd64,
        clk_a,clk_b,data,valid,1'b1,stream_resetn,overflow,clock_shift);
    always #5 encode_clk=~encode_clk;
    initial begin #4.125;forever #5 sample_clk=~sample_clk;end
    always @(posedge clk_a)begin edges_a=edges_a+1;a<=#6 a+1'b1;end
    always @(posedge clk_b)begin edges_b=edges_b+1;b<=#6 b+1'b1;end
    always @(posedge sample_clk)if(valid)begin
        words=words+1;
        if(check_words)begin
            if(data[15:8]!==data[7:0]+8'd64 || data[31:24]!==data[23:16]+8'd64)
                $fatal(1,"channel order at shift %d",clock_shift);
            if(have_previous && (data[7:0]!==previous[7:0]+8'd1 ||
                data[23:16]!==previous[23:16]+8'd1))
                $fatal(1,"duplicate or missing physical sample shift=%d old=%h new=%h",clock_shift,previous,data);
            previous=data;have_previous=1;
        end
    end
    initial begin
        #100;resetn=1;
        for(shift=6;shift>=0;shift=shift-1)begin
            enable=0;check_words=0;clock_shift=shift;#30000;
            begin_a=edges_a;begin_b=edges_b;#40000;
            if(edges_a-begin_a < (4000>>shift)-1 || edges_a-begin_a > (4000>>shift)+1 ||
               edges_b-begin_b < (4000>>shift)-1 || edges_b-begin_b > (4000>>shift)+1)
                $fatal(1,"physical clock rate shift=%d",shift);
            enable=1;#30000;have_previous=0;check_words=1;
            begin_words=words;#40000;
            if(overflow || words-begin_words < (4000>>shift)-1 || words-begin_words > (4000>>shift)+1)
                $fatal(1,"capture rate shift=%d words=%d",shift,words-begin_words);
        end
        passed=1;$display("PASS: physical clock ramp 1.5625..100 MHz, stable A/B pairs and STOP clocks");$finish;
    end
endmodule
