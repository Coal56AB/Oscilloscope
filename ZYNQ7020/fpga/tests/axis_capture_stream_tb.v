`timescale 1ns/1ps
// Continuous 400 MB/s source, finite FIFO, headers and periodic DMA stalls.
module axis_capture_stream_tb;
    reg clk=0, resetn=0, ready=0, passed=0;
    reg [31:0] group=0;
    wire source_ready, packed_valid, fifo_ready, frame_valid, last;
    wire [63:0] packed_data, frame_data;
    wire [7:0] keep;
    reg [63:0] fifo[0:31];
    integer wr=0, rd=0, used=0, peak=0, cycles=0, received=0;
    integer frame, offset;
    reg [63:0] expected;
    function [31:0] previous_bounds(input integer f);
        integer k,a,b,lo0,lo1,hi0,hi1;
        begin
            // The upper pair of every generated 32-bit counter is zero.
            lo0=0;lo1=0;hi0=0;hi1=0;
            for(k=0;k<48;k=k+1)begin
                a=((f-1)*48+k)%256;b=(((f-1)*48+k)/256)%256;
                if(a<lo0)lo0=a;if(a>hi0)hi0=a;
                if(b<lo1)lo1=b;if(b>hi1)hi1=b;
            end
            previous_bounds={8'(hi1),8'(hi0),8'(lo1),8'(lo0)};
        end
    endfunction
    wire push=packed_valid && used<32;
    wire pop=used>0 && fifo_ready;
    axis_capture_pack packer(clk,resetn,group,resetn,source_ready,
        packed_data,,packed_valid,used<32,);
    axis_capture_frame #(.BLOCK_BYTES(256)) dut(clk,resetn,32'd192,32'd200000000,fifo[rd],used>0,
        fifo_ready,frame_data,keep,frame_valid,ready,last,1'b0,18'd0);
    always #5 clk=~clk;
    initial begin repeat(3) @(negedge clk); resetn=1; end
    always @(negedge clk) if(resetn) begin
        cycles=cycles+1;
        ready=cycles%17<14;
        if(cycles>30000) $fatal(1,"stream timeout");
    end
    always @(posedge clk) if(resetn) begin
        if(!source_ready) $fatal(1,"ADC backpressure: FIFO accumulates headers");
        group<=group+1;
        if(push) begin fifo[wr]<=packed_data; wr<=(wr+1)%32; end
        if(pop) rd<=(rd+1)%32;
        case({push,pop})
            2'b10: used<=used+1;
            2'b01: used<=used-1;
        endcase
        if(used>peak) peak=used;
        if(frame_valid&&ready) begin
            frame=received/32; offset=received%32;
            case(offset)
                0: expected=64'h00000004_5043534f;
                1: expected={32'd200000000,32'd192};
                2: expected=frame;
                3: expected=frame*96;
                4: expected=0;
                5: expected=frame?frame-1:0;
                6: expected=frame?{32'h31584d4d,previous_bounds(frame)}:0;
                7: expected=frame?{32'd200000000,32'd192}:0;
                default: expected={32'(frame*48+(offset-8)*2+1),32'(frame*48+(offset-8)*2)};
            endcase
            if(frame_data!==expected || keep!==8'hff || last!==(offset==31))
                $fatal(1,"continuous stream data loss at beat %d",received);
            received=received+1;
            if(received==256*32) begin
                if(peak>16) $fatal(1,"unbounded queue growth");
                passed=1;
                $display("PASS: 256 continuous frames, DMA stalls, bounded FIFO peak=%d",peak);
                $finish;
            end
        end
    end
endmodule
