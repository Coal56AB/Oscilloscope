// ADCs remain clocked at full speed. Select uniformly spaced channel pairs,
// packing two selected pairs into each output word. Divider zero keeps A/B.
module axis_capture_decimate(
    (* X_INTERFACE_PARAMETER="XIL_INTERFACENAME clk, ASSOCIATED_BUSIF s_axis:m_axis, ASSOCIATED_RESET resetn" *)
    input wire clk, input wire resetn, input wire [31:0] group_divider,
    input wire [31:0] s_axis_tdata, input wire s_axis_tvalid,
    output wire s_axis_tready,
    output reg [31:0] m_axis_tdata, output reg m_axis_tvalid,
    input wire m_axis_tready);
    reg [31:0] remaining;
    reg [15:0] first_pair;
    reg half;
    wire space=!m_axis_tvalid || m_axis_tready;
    assign s_axis_tready=group_divider==0 ? space : (remaining!=0 || !half || space);
    always @(posedge clk) begin
        if (!resetn) begin
            remaining<=0; first_pair<=0; half<=0;
            m_axis_tdata<=0; m_axis_tvalid<=0;
        end else begin
            if (space) m_axis_tvalid<=0;
            if (s_axis_tvalid && s_axis_tready) begin
                if (group_divider==0) begin
                    m_axis_tdata<=s_axis_tdata; m_axis_tvalid<=1;
                end else if (remaining!=0) remaining<=remaining-1'b1;
                else begin
                    remaining<=group_divider-1'b1;
                    if (!half) first_pair<=s_axis_tdata[15:0];
                    else begin
                        m_axis_tdata<={s_axis_tdata[15:0],first_pair};
                        m_axis_tvalid<=1;
                    end
                    half<=!half;
                end
            end
        end
    end
endmodule
