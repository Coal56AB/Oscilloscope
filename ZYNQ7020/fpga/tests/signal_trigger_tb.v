`timescale 1ns/1ps
module signal_trigger_tb;
    reg clk=0,resetn=0,valid=0,pair_only=0,enabled=1,passed=0;
    reg [63:0] first_sample,minimum_sample=512,holdoff_samples=0;
    reg [51:0] sums;
    reg [91:0] variances;
    reg [31:0] centers,noise_power,arm_id=1;
    reg [1:0] filter_level,edge_select;
    reg signed [31:0] level=32'd10240000;
    reg [23:0] hysteresis=24'd131072;
    wire result_valid;
    wire [31:0] result_arm_id;
    wire [63:0] result_sample;
    signal_trigger dut(clk,resetn,valid,pair_only,first_sample,sums,variances,centers,noise_power,
        filter_level,edge_select,level,hysteresis,enabled,arm_id,minimum_sample,holdoff_samples,
        result_valid,result_arm_id,result_sample);
    always #5 clk=~clk;
    real denoised[0:4159],filtered[0:4159];
    real power,variance,mean,value,previous,crossing;
    integer channel,phase,mode,direction,noise_case,n,k,j,x,s,sq,radius,length,cycles,checked,rearm_at;
    integer expected_first,expected_second,saved_first,side,current_side,pending,wanted;
    function integer code(input integer index);
        integer p;
        begin
            p=index%256;
            code=(p<39?100:p<96?180:p<112?156:p<161?180:100)+(index%2?5:-5)+channel*3;
            if(index%997==233)code=255;
        end
    endfunction
    function integer find_first(input integer minimum);
        real previous,value,crossing;
        integer side,next_side,pending,dir,i,sample;
        begin
            previous=filtered[8+radius];side=previous<=154.25?-1:previous>=158.25?1:0;
            pending=0;find_first=-1;
            for(i=9+radius;i<4096-radius;i=i+1)begin
                value=filtered[i];dir=0;
                if(!pending && ((side<0 && previous<=156.25 && value>156.25) ||
                   (side>0 && previous>=156.25 && value<156.25)))begin
                    crossing=i-1+(156.25-previous)/(value-previous);pending=1;
                end
                next_side=value<=154.25?-1:value>=158.25?1:0;
                if(next_side!=0)begin
                    if(side!=0 && side!=next_side && pending)dir=next_side;
                    side=next_side;pending=0;
                end
                sample=$rtoi($floor(crossing+0.5));
                if(find_first<0 && dir!=0 && sample>=minimum &&
                    (direction==2 || (direction==0 && dir>0) || (direction==1 && dir<0)))find_first=sample;
                previous=value;
            end
        end
    endfunction
    initial begin
        for(channel=0;channel<2;channel=channel+1)for(phase=0;phase<2;phase=phase+1)
        for(mode=0;mode<4;mode=mode+1)for(direction=0;direction<3;direction=direction+1)
        for(noise_case=0;noise_case<2;noise_case=noise_case+1)begin
            @(negedge clk);resetn=0;valid=0;pair_only=phase;filter_level=mode;edge_select=direction;
            noise_power=noise_case?2598502:0;power=noise_power/65536.0;
            arm_id=1;minimum_sample=512;holdoff_samples=0;
            n=8;cycles=0;checked=0;rearm_at=0;
            radius=mode==0?0:mode==1?2:mode==2?8:32;length=2*radius+1;
            for(integer at=8;at<4160;at=at+1)begin
                s=0;sq=0;
                for(integer delta=-8;delta<=8;delta=delta+1)begin x=code(at+delta);s=s+x;sq=sq+x*x;end
                mean=s/17.0;variance=(17.0*sq-s*s)/289.0;value=code(at);
                denoised[at]=power>0 && variance<9*power?(variance>power?mean+(1-power/variance)*(value-mean):mean):value;
            end
            for(integer at=8+radius;at<4160-radius;at=at+1)begin
                value=0;for(integer delta=-radius;delta<=radius;delta=delta+1)value=value+denoised[at+delta];
                filtered[at]=value/length;
            end
            expected_first=find_first(512);expected_second=find_first(expected_first+2001>2048?expected_first+2001:2048);
            if(expected_first<0 || expected_second<0)$fatal(1,"bad stimulus");
            repeat(3)@(negedge clk);resetn=1;
            while(n<4096)begin
                @(negedge clk);cycles=cycles+1;valid=cycles%13<10;first_sample=n;
                for(k=0;k<4;k=k+1)begin
                    s=0;sq=0;
                    for(j=-8;j<=8;j=j+1)begin x=code(n+k+j);s=s+x;sq=sq+x*x;end
                    centers[k*8+:8]=code(n+k);sums[k*13+:13]=s;variances[k*23+:23]=17*sq-s*s;
                end
                if(valid)n=n+(phase?2:4);
                if(result_valid && result_arm_id==1 && checked==0)begin
                    if(result_sample!=expected_first)$fatal(1,"first ch %0d phase %0d filter %0d edge_select %0d noise %0d got %0d expected %0d",channel,phase,mode,direction,noise_case,result_sample,expected_first);
                    checked=1;saved_first=result_sample;rearm_at=cycles+100;
                end
                if(checked==1 && arm_id==1)begin
                    if(!result_valid || result_sample!=saved_first)$fatal(1,"first event overwritten");
                    if(cycles==rearm_at)begin arm_id=2;minimum_sample=2048;holdoff_samples=2000;end
                end
                if(result_valid && result_arm_id==2 && checked==1)begin
                    if(result_sample!=expected_second)$fatal(1,"rearm got %0d expected %0d",result_sample,expected_second);
                    checked=2;
                end
                if(cycles>10000)$fatal(1,"timeout");
            end
            @(negedge clk);valid=0;repeat(100)@(negedge clk);
            if(checked!=2)$fatal(1,"missing event");
        end
        passed=1;$display("PASS: Wiener + manual FILTER + armed Schmitt trigger, both channels/directions, PeakDetect, holdoff, rearm and first-event retention");$finish;
    end
endmodule
