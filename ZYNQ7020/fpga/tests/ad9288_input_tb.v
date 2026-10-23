`timescale 1ns/1ps
module ad9288_input_tb;
    reg encode_clk=0, sample_clk=0, resetn=0, enable=0, ready=1;
    reg [7:0] a=0, b=0;
    wire [31:0] data;
    wire clk_a, clk_b, valid, stream_resetn, overflow;
    wire input_ready, packed_valid;
    wire [63:0] packed_data;
    reg [31:0] expected[0:1023];
    integer written=0, read=0;
    integer count_a=0, count_b=0, received=0;
    reg passed=0;
    ad9288_input dut(encode_clk,sample_clk,resetn,enable,a,b,a+8'd64,b+8'd64,
        clk_a,clk_b,data,valid,input_ready,stream_resetn,overflow);
    // Exercise the real IP reset latency, not an always-ready receiver.
    adc_width_test width_converter(.aclk(sample_clk), .aresetn(stream_resetn),
        .s_axis_tdata(data), .s_axis_tvalid(valid), .s_axis_tready(input_ready),
        .m_axis_tdata(packed_data), .m_axis_tvalid(packed_valid), .m_axis_tready(ready));
    always #5 encode_clk=~encode_clk;
    initial begin #4.125; forever #5 sample_clk=~sample_clk; end
    // AD9288 output changes six ns after its own encode edge.
    always @(posedge clk_a) begin count_a=count_a+1; a<=#6 count_a; end
    always @(posedge clk_b) begin count_b=count_b+1; b<=#6 count_b; end
    always @(posedge sample_clk) if(valid&&input_ready) begin
        if(data[15:8] !== data[7:0]+8'd64 || data[31:24] !== data[23:16]+8'd64)
            $fatal(1,"channel order");
        if(data[7:0] !== data[23:16]) $fatal(1,"A/B phase pairing: %h",data);
        received=received+1;
        expected[written]=data; written=written+1;
    end
    always @(posedge sample_clk) begin
        if(!stream_resetn) begin written=0; read=0; end
        else if(packed_valid&&ready) begin
            if(packed_data!=={expected[read+1],expected[read]}) $fatal(1,"width converter data loss");
            read=read+2;
        end
    end
    initial begin
        #50; resetn=1; enable=1;
        #1000; if(received<50 || overflow) $fatal(1,"ADC not read");
        ready=0; #200;
        if(!overflow || valid) $fatal(1,"overflow did not stop source");
        enable=0; #100;
        if(overflow || stream_resetn || count_a<100 || count_b<100) $fatal(1,"stop/clock");
        ready=1; enable=1; #500;
        if(overflow || !valid) $fatal(1,"rearm failed");
        passed=1; $display("PASS: phase-shifted ADC reads, continuous clocks, overflow, rearm"); $finish;
    end
endmodule
