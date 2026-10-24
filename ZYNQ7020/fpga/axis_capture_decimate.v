// Full rate keeps A/B. Decimation preserves two uniform samples plus the
// min/max of both channels over their complete two-window interval.
module axis_capture_decimate(
    (* X_INTERFACE_PARAMETER="XIL_INTERFACENAME clk, ASSOCIATED_BUSIF s_axis:m_axis, ASSOCIATED_RESET resetn" *)
    input wire clk, input wire resetn, input wire [31:0] group_divider,
    input wire [31:0] s_axis_tdata, input wire s_axis_tvalid,
    output wire s_axis_tready,
    output wire [31:0] m_axis_tdata, output wire m_axis_tvalid,
    input wire m_axis_tready,
    output wire peak_detect);
    reg [31:0] position;
    reg [15:0] first_pair, second_pair;
    reg [7:0] low0, low1, high0, high1;
    reg [63:0] output_data;
    reg [1:0] output_words;
    wire last=position==((group_divider<<1)-1'b1);
    wire space=output_words==0 || (output_words==1 && m_axis_tready);
    wire [7:0] a0=s_axis_tdata[7:0], a1=s_axis_tdata[15:8];
    wire [7:0] b0=s_axis_tdata[23:16], b1=s_axis_tdata[31:24];
    wire [7:0] min0=a0<b0?a0:b0, min1=a1<b1?a1:b1;
    wire [7:0] max0=a0>b0?a0:b0, max1=a1>b1?a1:b1;
    wire [7:0] next_low0=position==0 || min0<low0?min0:low0;
    wire [7:0] next_low1=position==0 || min1<low1?min1:low1;
    wire [7:0] next_high0=position==0 || max0>high0?max0:high0;
    wire [7:0] next_high1=position==0 || max1>high1?max1:high1;
    assign peak_detect=group_divider!=0;
    assign m_axis_tdata=output_data[31:0];
    assign m_axis_tvalid=output_words!=0;
    assign s_axis_tready=peak_detect?(!last || space):space;
    always @(posedge clk) begin
        if (!resetn) begin
            position<=0; first_pair<=0; second_pair<=0;
            low0<=0;low1<=0;high0<=0;high1<=0;
            output_data<=0;output_words<=0;
        end else begin
            if (m_axis_tvalid && m_axis_tready) begin
                output_data<=output_data>>32;output_words<=output_words-1'b1;
            end
            if (s_axis_tvalid && s_axis_tready) begin
                if (!peak_detect) begin
                    output_data<={32'b0,s_axis_tdata};output_words<=1;
                end else begin
                    low0<=next_low0;low1<=next_low1;
                    high0<=next_high0;high1<=next_high1;
                    if (position==0) first_pair<=s_axis_tdata[15:0];
                    if (position==group_divider) second_pair<=s_axis_tdata[15:0];
                    if (last) begin
                        output_data<={next_high1,next_high0,next_low1,next_low0,
                            (position==group_divider?s_axis_tdata[15:0]:second_pair),first_pair};
                        output_words<=2;position<=0;
                    end else position<=position+1'b1;
                end
            end
        end
    end
endmodule
