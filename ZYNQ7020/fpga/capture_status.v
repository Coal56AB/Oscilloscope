module capture_status(input wire clk, input wire overflow,
                      output wire [31:0] status);
    (* ASYNC_REG = "TRUE" *) reg meta=0, synced=0;
    always @(posedge clk) begin meta<=overflow; synced<=meta; end
    assign status={16'hadc3,15'b0,synced};
endmodule
