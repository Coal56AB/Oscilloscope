// AD9288 S1=1, S2=0, DFS=0. ENCB is half a cycle after ENCA.
// Both ADCs remain clocked even when acquisition is stopped.
module ad9288_input(
    input wire encode_clk,
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME sample_clk, ASSOCIATED_BUSIF m_axis, ASSOCIATED_RESET resetn:stream_resetn" *)
    input wire sample_clk, input wire resetn,
    input wire enable,
    input wire [7:0] adc0_a, adc0_b, adc1_a, adc1_b,
    output wire adc_clk_a, adc_clk_b,
    output wire [31:0] m_axis_tdata,
    output wire m_axis_tvalid, input wire m_axis_tready,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 stream_resetn RST",
       X_INTERFACE_PARAMETER = "XIL_INTERFACENAME stream_resetn, POLARITY ACTIVE_LOW, TYPE PERIPHERAL" *)
    output wire stream_resetn, output reg overflow,
    input wire [2:0] clock_shift
);
    // Startup diagnostic: physical encode division with a fixed receive clock.
    // Shift zero retains the normal 100 MHz A/B interleave.
    reg [6:0] encode_count;
    reg [2:0] previous_shift,encode_shift;
    wire [6:0] encode_period=7'd1 << encode_shift;
    wire slow_high=encode_count < (encode_period >> 1);
    always @(posedge encode_clk) begin
        if(!resetn)encode_shift<=6;
        else encode_shift<=clock_shift;
        previous_shift<=encode_shift;
        if (!resetn || previous_shift!=encode_shift || encode_count==encode_period-1'b1)
            encode_count<=0;
        else encode_count<=encode_count+1'b1;
    end
    ODDR #(.DDR_CLK_EDGE("SAME_EDGE")) clock_a(
        .C(encode_clk), .CE(1'b1), .D1(encode_shift==0 ? 1'b1 : slow_high), .D2(encode_shift==0 ? 1'b0 : slow_high),
        .Q(adc_clk_a), .R(1'b0), .S(1'b0));
    ODDR #(.DDR_CLK_EDGE("SAME_EDGE")) clock_b(
        .C(encode_clk), .CE(1'b1), .D1(encode_shift==0 ? 1'b0 : !slow_high), .D2(encode_shift==0 ? 1'b1 : !slow_high),
        .Q(adc_clk_b), .R(1'b0), .S(1'b0));
    (* IOB = "TRUE" *) reg [15:0] sample_a, sample_b;
    reg [31:0] group_data;
    (* ASYNC_REG = "TRUE" *) reg enable_meta, enable_sync;
    reg [11:0] warmup;
    reg group_valid;
    wire warm=warmup >= (12'd32 << clock_shift);
    always @(posedge sample_clk) sample_a <= {adc1_a, adc0_a};
    always @(negedge sample_clk) sample_b <= {adc1_b, adc0_b};
    always @(posedge sample_clk) begin
        if (!resetn) begin enable_meta<=0; enable_sync<=0; end
        else begin enable_meta<=enable; enable_sync<=enable_meta; end
        group_data <= {sample_b, sample_a};
        // Divided encode still uses a 100 MHz receiver. Allow the forwarded
        // clock, ADC tPD and input return path to settle before taking B.
        group_valid<=encode_shift==0 ||
            encode_count==(((encode_period >> 1)+7'd3)&(encode_period-1'b1));
        if (!resetn || !enable_sync) begin warmup<=0; overflow<=0; end
        else if (!warm) warmup<=warmup+1'b1;
        else if (m_axis_tvalid && !m_axis_tready) overflow<=1;
    end
    // Release downstream IP before sending samples: AXIS READY takes clock
    // cycles to recover from reset. Warmup covers that startup latency.
    assign stream_resetn=resetn && enable_sync;
    assign m_axis_tdata=group_data;
    assign m_axis_tvalid=stream_resetn && warm && group_valid && !overflow;
endmodule
