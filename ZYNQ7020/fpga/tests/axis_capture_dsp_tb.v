`timescale 1ns/1ps
module axis_capture_dsp_tb;
    reg clk=0,resetn=0,valid=0,ready=0,peaks=0,passed=0;
    reg [287:0] configuration;
    reg [63:0] data;
    wire input_ready,output_valid,last;
    wire [63:0] output_data;
    wire [7:0] keep;
    integer phase,sent,received,cycles,frame,offset,sample,at,word_index;
    reg rearm,held;
    reg [63:0] expected,held_data;
    reg held_last;
    axis_capture_frame dut(clk,resetn,32'd4096,32'd50000000,data,valid,input_ready,
        output_data,keep,output_valid,ready,last,peaks,18'd0,configuration);
    always #5 clk=~clk;
    function [7:0] code(input integer index,input integer channel);
        begin code=channel==phase?(index%256<96 || index%256>=192?100:180):220;end
    endfunction
    initial begin
        for(phase=0;phase<2;phase=phase+1)begin
            @(negedge clk);resetn=0;valid=0;ready=0;peaks=phase;
            sent=0;received=0;cycles=0;rearm=0;held=0;configuration=0;
            configuration[31:0]=1|(phase<<1);configuration[95:64]=140*65536;
            configuration[127:96]=2*65536;configuration[191:128]=64;configuration[287:256]=1;
            repeat(3)@(negedge clk);resetn=1;
            wait(received==8*520);
        end
        passed=1;$display("PASS: TRG1 exact trigger headers, atomic rearm during header, both channels, raw/PeakDetect, payload and stalls");$finish;
    end
    always @(negedge clk)if(resetn)begin
        cycles=cycles+1;ready=cycles%13<9;
        if(!valid || input_ready)valid=sent<8*512 && cycles%11<8;
        sample=sent*(peaks?2:4);
        for(integer lane=0;lane<4;lane=lane+1)for(integer ch=0;ch<2;ch=ch+1)
            data[lane*16+ch*8+:8]=code(sample+lane,ch);
        if(peaks)data[63:32]=32'hff00ff00;
        if(rearm)begin configuration[191:128]=4096;configuration[287:256]=2;end
        if(cycles>20000)$fatal(1,"timeout");
    end
    always @(posedge clk)if(resetn)begin
        if(held && (!output_valid || output_data!==held_data || last!==held_last))$fatal(1,"unstable output");
        held=output_valid && !ready;held_data=output_data;held_last=last;
        if(valid && input_ready)sent=sent+1;
        if(output_valid && ready)begin
            frame=received/520;offset=received%520;
            case(offset)
                0:expected=64'h00000004_5043534f;
                1:expected={32'd50000000,32'd4096};
                2:expected=frame;
                3:expected=frame*(peaks?1024:2048);
                4:expected={frame<2?32'd1:32'd2,peaks?32'd2:32'd0};
                5:expected=frame==0 || (frame>=2 && frame<(peaks?5:3))?64'hffffffffffffffff:frame==1?96:4192;
                6:expected=1|(phase<<1);
                7:expected={31'd0,!(frame==0 || (frame>=2 && frame<(peaks?5:3))),32'h31475254};
                default:begin
                    at=(frame*512+offset-8)*(peaks?2:4);
                    for(integer lane=0;lane<4;lane=lane+1)for(integer ch=0;ch<2;ch=ch+1)
                        expected[lane*16+ch*8+:8]=code(at+lane,ch);
                    if(peaks)expected[63:32]=32'hff00ff00;
                end
            endcase
            if(output_data!==expected || keep!==255 || last!==(offset==519))
                $fatal(1,"phase %0d frame %0d offset %0d got %h expected %h",phase,frame,offset,output_data,expected);
            if(frame==1 && offset==4)rearm=1;
            received=received+1;
        end
    end
endmodule
