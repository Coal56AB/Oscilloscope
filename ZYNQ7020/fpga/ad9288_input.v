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
    output wire stream_resetn, output reg overflow
);
    ODDR #(.DDR_CLK_EDGE("SAME_EDGE")) clock_a(
        .C(encode_clk), .CE(1'b1), .D1(1'b1), .D2(1'b0),
        .Q(adc_clk_a), .R(1'b0), .S(1'b0));
    ODDR #(.DDR_CLK_EDGE("SAME_EDGE")) clock_b(
        .C(encode_clk), .CE(1'b1), .D1(1'b0), .D2(1'b1),
        .Q(adc_clk_b), .R(1'b0), .S(1'b0));
    (* IOB = "TRUE" *) reg [15:0] sample_a, sample_b;
    reg [31:0] group_data;
    (* ASYNC_REG = "TRUE" *) reg enable_meta, enable_sync;
    reg [5:0] warmup;
    always @(posedge sample_clk) sample_a <= {adc1_a, adc0_a};
    always @(negedge sample_clk) sample_b <= {adc1_b, adc0_b};
    always @(posedge sample_clk) begin
        if (!resetn) begin enable_meta<=0; enable_sync<=0; end
        else begin enable_meta<=enable; enable_sync<=enable_meta; end
        group_data <= {sample_b, sample_a};
        if (!resetn || !enable_sync) begin warmup<=0; overflow<=0; end
        else if (!warmup[5]) warmup<=warmup+1'b1;
        else if (m_axis_tvalid && !m_axis_tready) overflow<=1;
    end
    // Release downstream IP before sending samples: AXIS READY takes clock
    // cycles to recover from reset. Warmup covers that startup latency.
    assign stream_resetn=resetn && enable_sync;
    assign m_axis_tdata=group_data;
    assign m_axis_tvalid=stream_resetn && warmup[5] && !overflow;
endmodule
