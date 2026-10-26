`timescale 1ns/1ps
module capture_trigger_probe_tb;
    reg clk=0,resetn=0,valid=0,first=0,last=0,peaks=0,passed=0;
    reg [17:0] control=0;
    reg [63:0] seq=0,centers;
    reg [103:0] sums;
    reg [183:0] variances;
    wire result_valid;
    wire [63:0] result_seq;
    wire [1:0] result_sides;
    reg [1:0] expected[0:5];
    integer phase,channel,mode,sent,cycles,checked,k,ch,j,i,b,x,sum,sq,v,index;
    real mean,variance,power,value;
    function integer code(input integer at,input integer channel_index);
        integer block_index,p;
        begin
            block_index=at<0?0:at/64;p=at%64;
            code=170+(at%2?8:-8)+channel_index*5;
            if(block_index==2)code=(p<32?100:180)+channel_index*5;
            if(block_index==3 && p==31)code=149+channel_index*5;
            if(block_index==4)code=100+channel_index*5;
        end
    endfunction
    capture_trigger_probe dut(clk,resetn,control,valid,first,last,peaks,seq,sums,variances,centers,
        result_valid,result_seq,result_sides);
    always #5 clk=~clk;
    initial begin
        for(phase=0;phase<2;phase=phase+1)for(channel=0;channel<2;channel=channel+1)
        for(mode=0;mode<3;mode=mode+1)begin
            @(negedge clk);resetn=0;valid=0;peaks=phase;sent=0;cycles=0;checked=1;
            control=(1<<17)|(channel<<16)|((mode==0?0:mode==1?39:255)<<8)|156;
            for(b=0;b<6;b=b+1)begin
                expected[b]=0;
                for(i=0;i<64;i=i+1)begin
                    sum=0;sq=0;
                    for(j=-8;j<=8;j=j+1)begin x=code(b*64+i+j,channel);sum=sum+x;sq=sq+x*x;end
                    mean=sum/17.0;variance=(17.0*sq-sum*sum)/289.0;
                    value=code(b*64+i,channel);power=39.5;
                    if(mode==2)value=mean;
                    else if(mode==1 && variance<9*power)
                        value=variance>power?mean+(1-power/variance)*(value-mean):mean;
                    if(value<=155)expected[b][0]=1;
                    if(value>=157)expected[b][1]=1;
                end
            end
            repeat(3)@(negedge clk);resetn=1;wait(checked==6);
        end
        passed=1;$display("PASS: conservative Wiener sides, raw/mean modes, both channels, PeakDetect, stalls");$finish;
    end
    always @(negedge clk)if(resetn)begin
        cycles=cycles+1;valid=sent<6*64 && cycles%11<7;
        seq=sent/64;first=sent%64==0;last=sent%64+(peaks?2:4)==64;
        for(ch=0;ch<2;ch=ch+1)for(k=0;k<4;k=k+1)begin
            index=sent+k;sum=0;sq=0;
            for(j=-8;j<=8;j=j+1)begin x=code(index+j,ch);sum=sum+x;sq=sq+x*x;end
            v=17*sq-sum*sum;
            centers[(ch*4+k)*8+:8]=8'(code(index,ch));
            sums[(ch*4+k)*13+:13]=13'(sum);variances[(ch*4+k)*23+:23]=23'(v);
        end
        if(cycles>5000)$fatal(1,"timeout");
    end
    always @(posedge clk)if(resetn)begin
        if(valid)sent=sent+(peaks?2:4);
        if(result_valid && result_seq==checked)begin
            if((result_sides&expected[checked])!==expected[checked])
                $fatal(1,"lost side mode %0d channel %0d block %0d got %b expected %b",mode,channel,checked,result_sides,expected[checked]);
            if(mode!=1 && result_sides!==expected[checked])$fatal(1,"raw/mean mode mismatch");
            if(mode==1 && checked==3 && result_sides!==2)$fatal(1,"quiet spike not rejected");
            checked=checked+1;
        end
    end
endmodule
