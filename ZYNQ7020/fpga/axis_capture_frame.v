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
    output reg m_axis_tlast
);
    wire [31:0] payload_beats=payload_bytes >> 3;

    reg [3:0] header;
    reg [31:0] beat;
    reg [63:0] sequence, first_sample;
    wire space=!m_axis_tvalid || m_axis_tready;
    assign m_axis_tkeep=8'hff;
    assign s_axis_tready=header==8 && space;
    always @(posedge clk) begin
        if (!resetn) begin
            header<=0; beat<=0; sequence<=0; first_sample<=0;
            m_axis_tvalid<=0; m_axis_tlast<=0; m_axis_tdata<=0;
        end else begin
            if (space) begin
                m_axis_tvalid<=0; m_axis_tlast<=0;
                if (header<8) begin
                    m_axis_tvalid<=1;
                    case (header)
                        0: m_axis_tdata<=64'h00000003_5043534f;
                        1: m_axis_tdata<={sample_rate,payload_bytes};
                        2: m_axis_tdata<=sequence;
                        3: m_axis_tdata<=first_sample;
                        default: m_axis_tdata<=0;
                    endcase
                    header<=header+1'b1;
                end
            end
            if (s_axis_tvalid && s_axis_tready) begin
                    m_axis_tdata<=s_axis_tdata;
                    m_axis_tvalid<=1;
                    m_axis_tlast<=beat==payload_beats-1;
                    if (beat==payload_beats-1) begin
                        beat<=0; header<=0; sequence<=sequence+1'b1;
                        first_sample<=first_sample+(payload_bytes >> 1);
                    end else beat<=beat+1'b1;
            end
        end
    end
endmodule
