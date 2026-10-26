// 64-byte hardware header followed by packed uint8 ADC samples.
// Input FIFO stores every group; upstream overflow is fatal for this run.
module axis_capture_frame #(
    parameter integer BLOCK_BYTES=2097152
)(
    (* X_INTERFACE_PARAMETER = "XIL_INTERFACENAME clk, ASSOCIATED_BUSIF s_axis:m_axis, ASSOCIATED_RESET resetn" *)
    input wire clk, input wire resetn,
    input wire [31:0] payload_bytes, input wire [31:0] sample_rate,
    input wire [63:0] s_axis_tdata,
    input wire s_axis_tvalid, output wire s_axis_tready,
    output reg [63:0] m_axis_tdata,
    output wire [7:0] m_axis_tkeep,
    output reg m_axis_tvalid, input wire m_axis_tready,
    output reg m_axis_tlast, input wire peak_detect,input wire [17:0] probe_control
);
    wire [31:0] payload_beats=payload_bytes >> 3;

    reg [3:0] header;
    reg [31:0] beat;
    reg [63:0] sequence, first_sample;
    // Optional, backwards-compatible summary of the preceding payload. It is
    // ready when the next header is emitted; payload and TLAST stay unchanged.
    reg bounds_valid;
    reg [31:0] previous_bounds, previous_bytes, previous_rate;
    reg [63:0] previous_sequence;
    reg [7:0] low0,low1,high0,high1;
    reg bounds_beat_valid,bounds_beat_first,bounds_beat_last;
    reg [7:0] beat_low0_reg,beat_low1_reg,beat_high0_reg,beat_high1_reg;
    function [7:0] min2; input [7:0] a,b; begin min2=a<b?a:b; end endfunction
    function [7:0] max2; input [7:0] a,b; begin max2=a>b?a:b; end endfunction
    wire [7:0] lo0=min2(s_axis_tdata[7:0],s_axis_tdata[23:16]);
    wire [7:0] lo1=min2(s_axis_tdata[15:8],s_axis_tdata[31:24]);
    wire [7:0] hi0=max2(s_axis_tdata[7:0],s_axis_tdata[23:16]);
    wire [7:0] hi1=max2(s_axis_tdata[15:8],s_axis_tdata[31:24]);
    wire [7:0] beat_low0=peak_detect?lo0:min2(lo0,min2(s_axis_tdata[39:32],s_axis_tdata[55:48]));
    wire [7:0] beat_low1=peak_detect?lo1:min2(lo1,min2(s_axis_tdata[47:40],s_axis_tdata[63:56]));
    wire [7:0] beat_high0=peak_detect?hi0:max2(hi0,max2(s_axis_tdata[39:32],s_axis_tdata[55:48]));
    wire [7:0] beat_high1=peak_detect?hi1:max2(hi1,max2(s_axis_tdata[47:40],s_axis_tdata[63:56]));
    wire [7:0] next_low0=bounds_beat_first?beat_low0_reg:min2(low0,beat_low0_reg);
    wire [7:0] next_low1=bounds_beat_first?beat_low1_reg:min2(low1,beat_low1_reg);
    wire [7:0] next_high0=bounds_beat_first?beat_high0_reg:max2(high0,beat_high0_reg);
    wire [7:0] next_high1=bounds_beat_first?beat_high1_reg:max2(high1,beat_high1_reg);
    wire space=!m_axis_tvalid || m_axis_tready;
    wire window_valid;
    wire [63:0] window_sequence,window_ch0,window_ch1;
    wire [31:0] window_raw_bounds;
    wire group_valid,group_first,group_last,group_peak,probe_valid;
    wire [63:0] group_sequence,group_centers,probe_sequence;
    wire [103:0] group_sums;
    wire [183:0] group_variances;
    wire [1:0] probe_sides;
    wire use_window=payload_bytes>=4096 && window_valid && window_sequence+2==sequence;
    capture_window_stats window_stats(.clk(clk),.resetn(resetn),.accept(s_axis_tvalid && s_axis_tready),
        .peak_detect(peak_detect),.data(s_axis_tdata),.position(beat<<(peak_detect?1:2)),
        .sequence_id(sequence),.previous_raw_bounds(previous_bounds),.result_valid(window_valid),
        .result_sequence(window_sequence),.result_raw_bounds(window_raw_bounds),.result_ch0(window_ch0),.result_ch1(window_ch1),
        .group_valid(group_valid),.group_first(group_first),.group_last(group_last),.group_peak(group_peak),
        .group_sequence(group_sequence),.group_sums(group_sums),.group_variances(group_variances),.group_centers(group_centers));
    capture_trigger_probe trigger_probe(clk,resetn,probe_control,group_valid,group_first,group_last,group_peak,
        group_sequence,group_sums,group_variances,group_centers,probe_valid,probe_sequence,probe_sides);
    wire use_probe=use_window && probe_control[17] && probe_valid && probe_sequence==window_sequence;
    wire [63:0] header_ch0={use_probe?probe_control[14:0]:15'd0,window_ch0[48:0]};
    wire [63:0] header_ch1={use_probe?{10'd0,probe_sides,probe_control[17:15]}:15'd0,window_ch1[48:0]};
    assign m_axis_tkeep=8'hff;
    assign s_axis_tready=header==8 && space;
    always @(posedge clk) begin
        if (!resetn) begin
            header<=0; beat<=0; sequence<=0; first_sample<=0;
            m_axis_tvalid<=0; m_axis_tlast<=0; m_axis_tdata<=0;
            bounds_valid<=0;previous_bounds<=0;previous_bytes<=0;previous_rate<=0;previous_sequence<=0;
            low0<=0;low1<=0;high0<=0;high1<=0;
            bounds_beat_valid<=0;bounds_beat_first<=0;bounds_beat_last<=0;
            beat_low0_reg<=0;beat_low1_reg<=0;beat_high0_reg<=0;beat_high1_reg<=0;
        end else begin
            bounds_beat_valid<=s_axis_tvalid && s_axis_tready;
            if(bounds_beat_valid)begin
                low0<=next_low0;low1<=next_low1;high0<=next_high0;high1<=next_high1;
                if(bounds_beat_last)begin
                    bounds_valid<=1;previous_sequence<=sequence-1'b1;
                    previous_bounds<={next_high1,next_high0,next_low1,next_low0};
                    previous_rate<=sample_rate;previous_bytes<=payload_bytes;
                end
            end
            if (space) begin
                m_axis_tvalid<=0; m_axis_tlast<=0;
                if (header<8) begin
                    m_axis_tvalid<=1;
                    case (header)
                        0: m_axis_tdata<=64'h00000004_5043534f;
                        1: m_axis_tdata<={sample_rate,payload_bytes};
                        2: m_axis_tdata<=sequence;
                        3: m_axis_tdata<=first_sample;
                        4: m_axis_tdata<={use_window?window_raw_bounds:32'd0,peak_detect?32'd2:32'd0};
                        5: m_axis_tdata<=use_window?header_ch0:bounds_valid?previous_sequence:64'd0;
                        6: m_axis_tdata<=use_window?header_ch1:bounds_valid?{32'h31584d4d,previous_bounds}:64'd0;
                        7: m_axis_tdata<=use_window?{window_sequence[31:0],use_probe?32'h33584d4d:32'h32584d4d}:
                            bounds_valid?{previous_rate,previous_bytes}:64'd0;
                        default: m_axis_tdata<=0;
                    endcase
                    header<=header+1'b1;
                end
            end
            if (s_axis_tvalid && s_axis_tready) begin
                    bounds_beat_first<=beat==0;bounds_beat_last<=beat==payload_beats-1;
                    beat_low0_reg<=beat_low0;beat_low1_reg<=beat_low1;
                    beat_high0_reg<=beat_high0;beat_high1_reg<=beat_high1;
                    m_axis_tdata<=s_axis_tdata;
                    m_axis_tvalid<=1;
                    m_axis_tlast<=beat==payload_beats-1;
                    if (beat==payload_beats-1) begin
                        beat<=0; header<=0; sequence<=sequence+1'b1;
                        first_sample<=first_sample+(payload_bytes >> (peak_detect?2:1));
                    end else beat<=beat+1'b1;
            end
        end
    end
endmodule
