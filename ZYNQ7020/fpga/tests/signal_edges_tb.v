`timescale 1ns/1ps
module signal_edges_tb;
    reg clk=0,resetn=0,valid=0,passed=0;
    reg [3:0] mask;
    reg [63:0] first_sample;
    reg [95:0] values;
    reg signed [31:0] level=32'd10240000; // 156.25 codes
    reg [23:0] hysteresis;
    wire result_valid;
    wire [3:0] result_mask,result_rising;
    wire [255:0] bases;
    wire [95:0] numerators,denominators;
    signal_edges dut(clk,resetn,valid,mask,first_sample,values,level,hysteresis,
        result_valid,result_mask,result_rising,bases,numerators,denominators);
    always #5 clk=~clk;
    reg [63:0] expected_bases[0:32767];
    reg [23:0] expected_numerators[0:32767],expected_denominators[0:32767];
    reg expected_rising[0:32767];
    integer phase,mode,sent,received,cycles,lanes,k,x,previous,side,current_side,at;
    integer expected=0,pending=0,seen=0;
    reg [63:0] pending_base,index,next_index;
    reg [23:0] pending_numerator,pending_denominator;
    function integer code(input integer position);
        integer block_index,p;
        begin
            block_index=position/1024;p=position%1024;
            case(block_index%4)
                0:code=(p%2?180:100)*65536;
                1:code=(p<64?100:p<960?156:180)*65536;
                2:code=(p%64<32?155:157)*65536+(p%7)*4096;
                default:code=(p%128<32?100:p%128<64?156:p%128<96?180:156)*65536;
            endcase
        end
    endfunction
    task reference_sample(input integer value,input reg [63:0] index);
        integer direction;
        begin
            direction=0;current_side=value<=level-hysteresis?-1:value>=level+hysteresis?1:0;
            if(!seen || index!=next_index)begin side=current_side;pending=0;end
            else if(hysteresis==0)begin
                direction=previous<=level && value>level?1:previous>=level && value<level?-1:0;
                if(direction!=0)begin
                    pending_base=index-1;
                    pending_numerator=value>previous?level-previous:previous-level;
                    pending_denominator=value>previous?value-previous:previous-value;
                end
            end else begin
                if(!pending && ((side<0 && previous<=level && value>level) || (side>0 && previous>=level && value<level)))begin
                    pending=1;pending_base=index-1;
                    pending_numerator=value>previous?level-previous:previous-level;
                    pending_denominator=value>previous?value-previous:previous-value;
                end
                if(current_side!=0)begin
                    if(side!=0 && side!=current_side && pending)direction=current_side;
                    side=current_side;pending=0;
                end
            end
            if(direction!=0)begin
                expected_bases[expected]=pending_base;expected_numerators[expected]=pending_numerator;
                expected_denominators[expected]=pending_denominator;expected_rising[expected]=direction>0;expected=expected+1;
            end
            seen=1;previous=value;next_index=index+1;
        end
    endtask
    initial begin
        for(phase=0;phase<2;phase=phase+1)for(mode=0;mode<3;mode=mode+1)begin
            @(negedge clk);resetn=0;valid=0;lanes=phase?2:4;
            hysteresis=mode==0?0:mode==1?2*65536:8*65536;
            sent=0;received=0;expected=0;cycles=0;seen=0;pending=0;side=0;
            repeat(3)@(negedge clk);resetn=1;
            while(sent<8192)begin
                @(negedge clk);cycles=cycles+1;valid=cycles%13<9;
                first_sample=sent+(sent>=4096?1000:0);mask=phase?3:15;
                for(k=0;k<4;k=k+1)values[k*24+:24]=code(sent+k);
                if(sent%256==0)mask[0]=0;
                if(valid)begin
                    for(k=0;k<lanes;k=k+1)if(mask[k])reference_sample(code(sent+k),first_sample+k);
                    sent=sent+lanes;
                end
            end
            @(negedge clk);valid=0;repeat(5)@(negedge clk);
            if(received!=expected)$fatal(1,"lost edges phase %0d mode %0d %0d/%0d",phase,mode,received,expected);
        end
        passed=1;$display("PASS: ordered Schmitt edges, both directions, long pending crossings, fractional levels, gaps and reset");$finish;
    end
    always @(posedge clk)if(resetn && result_valid)begin
        for(integer lane=0;lane<4;lane=lane+1)if(result_mask[lane])begin
            if(received>=expected || bases[lane*64+:64]!==expected_bases[received] ||
               numerators[lane*24+:24]!==expected_numerators[received] ||
               denominators[lane*24+:24]!==expected_denominators[received] || result_rising[lane]!==expected_rising[received])
                $fatal(1,"edge %0d mismatch phase %0d mode %0d base %0d expected %0d",received,phase,mode,bases[lane*64+:64],expected_bases[received]);
            received=received+1;
        end
    end
endmodule
