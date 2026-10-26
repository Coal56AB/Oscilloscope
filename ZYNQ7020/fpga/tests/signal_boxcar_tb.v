`timescale 1ns/1ps
module signal_boxcar_tb;
    reg clk=0,resetn=0,valid=0,pair_only=0,passed=0;
    reg [1:0] level;
    reg [63:0] first_sample;
    reg [95:0] values;
    wire result_valid;
    wire [3:0] result_mask;
    wire [63:0] result_first_sample;
    wire [95:0] result_values;
    signal_boxcar dut(clk,resetn,valid,pair_only,level,first_sample,values,
        result_valid,result_mask,result_first_sample,result_values);
    always #5 clk=~clk;
    integer phase,mode,sent,received,cycles,lanes,radius,length,k,j;
    reg [63:0] total,wanted;
    function [23:0] code(input integer index);
        reg [31:0] mixed;
        begin
            mixed=(index*32'd16127)^32'h5ca716e9;
            code=index%97<31?24'hff0000:index%97<64?24'd0:mixed[23:0];
        end
    endfunction
    initial begin
        for(phase=0;phase<2;phase=phase+1)for(mode=0;mode<4;mode=mode+1)begin
            @(negedge clk);resetn=0;valid=0;pair_only=phase;level=mode;
            sent=0;received=0;cycles=0;lanes=phase?2:4;
            length=mode==0?1:mode==1?5:mode==2?17:65;radius=(length-1)/2;
            repeat(3)@(negedge clk);resetn=1;
            while(sent<4096)begin
                @(negedge clk);cycles=cycles+1;valid=cycles%11<8;first_sample=sent;
                for(k=0;k<4;k=k+1)values[k*24+:24]=code(sent+k);
                if(valid)sent=sent+lanes;
            end
            @(negedge clk);valid=0;repeat(24)@(negedge clk);
            if(received!=4096-length+1)$fatal(1,"lost samples phase %0d mode %0d got %0d",phase,mode,received);
        end
        passed=1;$display("PASS: centred 1/5/17/65 Q8.16 means, two/four lanes, stalls and reset");$finish;
    end
    always @(posedge clk)if(resetn && result_valid)begin
        for(integer lane=0;lane<4;lane=lane+1)if(result_mask[lane])begin
            if(result_first_sample+lane!=received+radius)$fatal(1,"sample index mismatch");
            total=0;
            for(integer at=0;at<length;at=at+1)total=total+code(received+at);
            wanted=(total+length/2)/length;
            if(result_values[lane*24+:24]!==wanted[23:0])
                $fatal(1,"phase %0d mode %0d index %0d got %0d expected %0d",phase,mode,received,result_values[lane*24+:24],wanted);
            received=received+1;
        end
    end
endmodule
