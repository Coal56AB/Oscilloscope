// Input group at 100 MHz: {CH2_B, CH1_B, CH2_A, CH1_A}.
// Byte lane 0 is the earliest CH1 sample; the DMA never expands uint8 samples.
module axis_capture_pack #(
    parameter integer BLOCK_BEATS = 262144
)(
    input wire clk, input wire resetn,
    input wire [31:0] s_axis_tdata,
    input wire s_axis_tvalid, output wire s_axis_tready,
    output reg [63:0] m_axis_tdata,
    output wire [7:0] m_axis_tkeep,
    output reg m_axis_tvalid, input wire m_axis_tready,
    output reg m_axis_tlast
);
    reg [31:0] first_group;
    reg have_first;
    reg [31:0] beat_index;
    assign m_axis_tkeep=8'hff;
    assign s_axis_tready=!have_first || !m_axis_tvalid || m_axis_tready;
    always @(posedge clk) begin
        if(!resetn) begin
            have_first<=0; m_axis_tvalid<=0; m_axis_tlast<=0;
            m_axis_tdata<=0; first_group<=0; beat_index<=0;
        end else begin
            if(m_axis_tvalid && m_axis_tready) m_axis_tvalid<=0;
            if(s_axis_tvalid && s_axis_tready) begin
                if(!have_first) begin first_group<=s_axis_tdata;have_first<=1;end
                else begin
                    m_axis_tdata<={s_axis_tdata,first_group};m_axis_tvalid<=1;have_first<=0;
                    m_axis_tlast<=beat_index==BLOCK_BEATS-1;
                    if(beat_index==BLOCK_BEATS-1)beat_index<=0;else beat_index<=beat_index+1;
                end
            end
        end
    end
endmodule
