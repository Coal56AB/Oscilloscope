module capture_status #(parameter HAS_ANALYSIS=0, HAS_FILTER=0)(input wire clk, input wire overflow,
                      output wire [31:0] status);
    (* ASYNC_REG = "TRUE" *) reg meta=0, synced=0;
    always @(posedge clk) begin meta<=overflow; synced<=meta; end
    // Bit 1 advertises the DSP register bank, before software touches its
    // address. Older bitstreams leave it clear and remain usable.
    // Bits 2/3 advertise DDR replay and its amplitude-only operation on HP1.
    // Bit 4 advertises the replay filter operation and its extended output.
    assign status={16'hadc5,11'b0,(HAS_FILTER!=0),(HAS_ANALYSIS!=0),(HAS_ANALYSIS!=0),1'b1,synced};
endmodule
