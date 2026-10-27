`timescale 1ns/1ps
module capture_window_header_tb;
    reg clk=0,resetn=0,valid=0,ready=0,peaks=0,passed=0;
    reg [17:0] probe_control=0;
    wire input_ready,output_valid,last;
    wire [63:0] output_data;
    wire [7:0] keep;
    wire [63:0] data=peaks?64'hff00ff00_80808080:64'h80808080_80808080;
    reg [63:0] expected;
    localparam [63:0] STATS={15'd0,23'd0,13'd2176,13'd2176};
    integer phase,sent=0,received=0,cycles=0,frame,offset;
    axis_capture_frame dut(clk,resetn,32'd4096,32'd50000000,data,valid,input_ready,
        output_data,keep,output_valid,ready,last,peaks,probe_control,288'd0);
    always #5 clk=~clk;
    initial begin
        for(phase=0;phase<8;phase=phase+1)begin
            @(negedge clk);resetn=0;valid=0;ready=0;peaks=phase%2;sent=0;received=0;cycles=0;
            probe_control=phase<2?0:(1<<17)|((phase<4?0:phase<6?39:255)<<8)|156;
            repeat(3)@(negedge clk);resetn=1;wait(received==5*520);
        end
        passed=1;$display("PASS: MMX1/MMX2 headers, delayed sequence, raw/PeakDetect, native peak exclusion");$finish;
    end
    always @(negedge clk)if(resetn)begin
        cycles=cycles+1;valid=sent<5*512 && cycles%7!=0;ready=cycles%11<8;
        if(cycles>20000)$fatal(1,"timeout");
    end
    always @(posedge clk)if(resetn)begin
        if(valid&&input_ready)sent=sent+1;
        if(output_valid&&ready)begin
            frame=received/520;offset=received%520;
            case(offset)
                0:expected=64'h00000004_5043534f;
                1:expected={32'd50000000,32'd4096};
                2:expected=frame;
                3:expected=frame*(peaks?1024:2048);
                4:expected={frame>=3?32'h80808080:32'd0,peaks?32'd2:32'd0};
                5:expected=frame>=3?(STATS|({49'd0,probe_control[14:0]}<<49)):frame?frame-1:0;
                6:expected=frame>=3?(STATS|(probe_control[17]?({49'd0,10'd0,2'b01,probe_control[17:15]}<<49):0)):
                    frame?64'h31584d4d_80808080:0;
                7:expected=frame>=3?{32'(frame-2),probe_control[17]?32'h33584d4d:32'h32584d4d}:
                    frame?{32'd50000000,32'd4096}:0;
                default:expected=data;
            endcase
            if(output_data!==expected || keep!==255 || last!==(offset==519))
                $fatal(1,"mode %d frame %d beat %d got %h expected %h",peaks,frame,offset,output_data,expected);
            received=received+1;
        end
    end
endmodule
