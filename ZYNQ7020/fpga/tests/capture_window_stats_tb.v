`timescale 1ns/1ps
module capture_window_stats_tb;
    reg clk=0,resetn=0,accept=0,peaks=0,passed=0;
    reg [63:0] data,seq=0;
    reg [31:0] position=0;
    wire valid;
    wire [63:0] result_seq,ch0,ch1;
    wire [31:0] raw;
    reg [63:0] expected[0:1][0:3];
    integer phase,ch,b,i,j,n,value,sum,squares,v,lo,hi,maxv,sent,cycles,checked;
    function integer code(input integer index,input integer channel);
        integer x;
        begin
            x=130+(index%2?20:-20)+(index*17+channel*23)%7-3;
            if(index>=1020 && index<1100)x=x+60;
            if(index==2047)x=250;
            code=x;
        end
    endfunction
    capture_window_stats dut(.clk(clk),.resetn(resetn),.accept(accept),.peak_detect(peaks),
        .data(data),.position(position),.sequence_id(seq),.previous_raw_bounds(32'h12345678),
        .result_valid(valid),.result_sequence(result_seq),.result_raw_bounds(raw),.result_ch0(ch0),.result_ch1(ch1));
    always #5 clk=~clk;
    initial begin
        for(ch=0;ch<2;ch=ch+1)for(b=1;b<=3;b=b+1)begin
            lo=8191;hi=0;maxv=0;
            for(i=0;i<1024;i=i+1)begin
                sum=0;squares=0;
                for(j=-8;j<=8;j=j+1)begin value=code(b*1024+i+j,ch);sum=sum+value;squares=squares+value*value;end
                v=17*squares-sum*sum;
                if(sum<lo)lo=sum;if(sum>hi)hi=sum;if(v>maxv)maxv=v;
            end
            expected[ch][b]={15'd0,23'(maxv),13'(hi),13'(lo)};
        end
        for(phase=0;phase<2;phase=phase+1)begin
            @(negedge clk);resetn=0;accept=0;peaks=phase;sent=0;checked=0;cycles=0;
            repeat(3)@(negedge clk);resetn=1;
            wait(checked==3);
        end
        passed=1;$display("PASS: exact centred sums/variances, A/B jitter, boundary impulses, PeakDetect, stalls");$finish;
    end
    always @(negedge clk)if(resetn)begin
        cycles=cycles+1;accept=sent<5*1024 && cycles%11<7;
        seq=sent/1024;position=sent%1024;
        for(n=0;n<4;n=n+1)for(j=0;j<2;j=j+1)data[n*16+j*8+:8]=8'(code(sent+n,j));
        if(peaks)data[63:32]=32'hff00ff00;
        if(cycles>10000)$fatal(1,"timeout");
    end
    always @(posedge clk)if(resetn)begin
        if(accept)sent=sent+(peaks?2:4);
        if(valid && result_seq==checked+1)begin
            if(ch0!==expected[0][result_seq] || ch1!==expected[1][result_seq] || raw!==32'h12345678)
                $fatal(1,"mode %0d seq %0d got %h/%h expected %h/%h",peaks,result_seq,ch0,ch1,expected[0][result_seq],expected[1][result_seq]);
            checked=checked+1;
        end
    end
endmodule
