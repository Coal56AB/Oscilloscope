// Software changes configuration only while acquisition is disabled.
module capture_config(input wire clk, input wire [31:0] control,
    output wire enable, output reg [31:0] sample_rate,
    output reg [31:0] group_divider, output wire [31:0] payload_bytes,
    output wire [2:0] clock_shift);
    (* ASYNC_REG="TRUE" *) reg [31:0] sync1=32'h3000, sync2=32'h3000;
    assign enable=control[0];
    assign payload_bytes=32'd8 << sync2[10:6];
    assign clock_shift=sync2[13:11];
    always @(posedge clk) begin
        sync1<=control; sync2<=sync1;
        if (clock_shift!=0) begin
            sample_rate<=200000000 >> clock_shift; group_divider<=0;
        end else case (sync2[5:1])
            0: begin sample_rate<=200000000; group_divider<=0; end
            1: begin sample_rate<=100000000; group_divider<=1; end
            2: begin sample_rate<=50000000; group_divider<=2; end
            3: begin sample_rate<=20000000; group_divider<=5; end
            4: begin sample_rate<=10000000; group_divider<=10; end
            5: begin sample_rate<=5000000; group_divider<=20; end
            6: begin sample_rate<=2000000; group_divider<=50; end
            7: begin sample_rate<=1000000; group_divider<=100; end
            8: begin sample_rate<=500000; group_divider<=200; end
            9: begin sample_rate<=200000; group_divider<=500; end
            10: begin sample_rate<=100000; group_divider<=1000; end
            11: begin sample_rate<=50000; group_divider<=2000; end
            12: begin sample_rate<=20000; group_divider<=5000; end
            13: begin sample_rate<=10000; group_divider<=10000; end
            14: begin sample_rate<=5000; group_divider<=20000; end
            15: begin sample_rate<=2000; group_divider<=50000; end
            16: begin sample_rate<=1000; group_divider<=100000; end
            17: begin sample_rate<=500; group_divider<=200000; end
            18: begin sample_rate<=200; group_divider<=500000; end
            default: begin sample_rate<=100; group_divider<=1000000; end
        endcase
    end
endmodule
