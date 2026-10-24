`timescale 1ns/1ps
module axis_capture_frame_tb;
    reg clk=0, resetn=0, valid=0, ready=0;
    reg [63:0] data=0;
    wire input_ready, output_valid, last;
    wire [63:0] output_data;
    wire [7:0] keep;
    integer sent=0, received=0, cycles=0, frame, offset;
    reg held=0, passed=0;
    reg [63:0] held_data, expected;
    reg held_last;
    axis_capture_frame #(.BLOCK_BYTES(128)) dut(
        clk,resetn,32'd64,32'd200000000,data,valid,input_ready,output_data,keep,output_valid,ready,last,1'b0);
    always #5 clk=~clk;
    initial begin repeat(3) @(negedge clk); resetn=1; end
    always @(negedge clk) if(resetn) begin
        cycles=cycles+1; ready=cycles%11<7; valid=sent<32;
        data={32'(2*sent+1),32'(2*sent)};
        if(cycles>1000) $fatal(1,"timeout");
    end
    always @(posedge clk) if(resetn) begin
        if(held && (!output_valid || output_data!==held_data || last!==held_last))
            $fatal(1,"unstable stalled output");
        held=output_valid&&!ready; held_data=output_data; held_last=last;
        if(valid&&input_ready) sent=sent+1;
        if(output_valid&&ready) begin
            frame=received/16; offset=received%16;
            case(offset)
                0: expected=64'h00000004_5043534f;
                1: expected={32'd200000000,32'd64};
                2: expected=frame;
                3: expected=frame*32;
                4,5,6,7: expected=0;
                default: expected={32'(frame*16+(offset-8)*2+1),32'(frame*16+(offset-8)*2)};
            endcase
            if(output_data!==expected || keep!==8'hff || last!==(offset==15))
                $fatal(1,"frame %d beat %d: %h expected %h",frame,offset,output_data,expected);
            received=received+1;
            if(received==64) begin
                passed=1;
                $display("PASS: ADC headers, first_sample, sequence, sample order, stalls, TLAST"); $finish;
            end
        end
    end
endmodule
