`timescale 1ns/1ps
module axis_capture_bounds_tb;
    reg clk=0,resetn=0,valid=0,ready=0,peaks=0;
    reg [63:0] data;
    wire input_ready,output_valid,last;
    wire [63:0] output_data;
    wire [7:0] keep;
    integer sent=0,received=0,cycles=0,frame,offset,phase,i,j,ch,value;
    integer lo[0:1],hi[0:1];
    reg [31:0] bounds[0:3];
    reg [63:0] expected,held_data;
    reg held=0,held_last,passed=0;
    function [63:0] sample_word(input integer n,input integer peak);
        reg [63:0] word_value;
        integer k;
        begin
            for(k=0;k<8;k=k+1)word_value[k*8+:8]=(n*17+k*31+13)%201+20;
            if(peak)word_value[63:32]=32'hff00ff00;
            sample_word=word_value;
        end
    endfunction
    axis_capture_frame dut(clk,resetn,32'd64,32'd50000000,data,valid,input_ready,
        output_data,keep,output_valid,ready,last,peaks,18'd0,288'd0);
    always #5 clk=~clk;
    initial begin
        for(phase=0;phase<2;phase=phase+1)begin
            @(negedge clk);resetn=0;valid=0;ready=0;peaks=phase;
            sent=0;received=0;cycles=0;held=0;
            for(i=0;i<4;i=i+1)begin
                lo[0]=255;lo[1]=255;hi[0]=0;hi[1]=0;
                for(j=0;j<8;j=j+1)begin
                    expected=sample_word(i*8+j,phase);
                    for(ch=0;ch<(phase?4:8);ch=ch+1)begin
                        value=expected[ch*8+:8];
                        if(value<lo[ch%2])lo[ch%2]=value;
                        if(value>hi[ch%2])hi[ch%2]=value;
                    end
                end
                bounds[i]={8'(hi[1]),8'(hi[0]),8'(lo[1]),8'(lo[0])};
            end
            repeat(3)@(negedge clk);resetn=1;
            wait(received==64);
        end
        passed=1;$display("PASS: raw/PeakDetect bounds, ignored peak envelope, stalls, reset");$finish;
    end
    always @(negedge clk)if(resetn)begin
        cycles=cycles+1;ready=cycles%13<8;valid=sent<32 && cycles%7!=0;
        data=sample_word(sent,peaks);
        if(cycles>2000)$fatal(1,"timeout");
    end
    always @(posedge clk)if(resetn)begin
        if(held && (!output_valid || output_data!==held_data || last!==held_last))$fatal(1,"stalled output changed");
        held=output_valid&&!ready;held_data=output_data;held_last=last;
        if(valid&&input_ready)sent=sent+1;
        if(output_valid&&ready)begin
            frame=received/16;offset=received%16;
            case(offset)
                0:expected=64'h00000004_5043534f;
                1:expected={32'd50000000,32'd64};
                2:expected=frame;
                3:expected=frame*(peaks?16:32);
                4:expected=peaks?2:0;
                5:expected=frame?frame-1:0;
                6:expected=frame?{32'h31584d4d,bounds[frame-1]}:0;
                7:expected=frame?{32'd50000000,32'd64}:0;
                default:expected=sample_word(frame*8+offset-8,peaks);
            endcase
            if(output_data!==expected || keep!==255 || last!==(offset==15))
                $fatal(1,"mode %0d frame %0d beat %0d got %h expected %h",peaks,frame,offset,output_data,expected);
            received=received+1;
        end
    end
endmodule
