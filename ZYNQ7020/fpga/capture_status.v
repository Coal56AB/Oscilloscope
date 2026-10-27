module capture_status(input wire clk, input wire overflow,
                      output wire [31:0] status);
    (* ASYNC_REG = "TRUE" *) reg meta=0, synced=0;
    always @(posedge clk) begin meta<=overflow; synced<=meta; end
    // Bit 1 advertises the DSP register bank, before software touches its
    // address. Older bitstreams leave it clear and remain usable.
    assign status={16'hadc5,14'b0,1'b1,synced};
endmodule
