`timescale 1ns/1ps
// The production decimator feeds the window statistics and hardware trigger.
module capture_dsp_decimate_tb;
    reg clk=0,resetn=0,valid=0,passed=0;
    reg [31:0] divider=0,data=0,rate=200000000;
    reg [287:0] settings=0;
    wire adc_ready,selected_valid,selected_ready,packed_valid,packed_ready,output_valid,last;
    wire [31:0] selected;
    wire [63:0] packed_data,output_data;
    wire [7:0] keep;
    wire peaks=divider!=0;
    integer epoch,cycle,sent,words,offset,frames,hits;
    reg [31:0] held_arm,held_control;
    reg [63:0] held_sample;
    wire output_ready=cycle%13<10;
    axis_capture_decimate decimate(clk,resetn,divider,data,valid,adc_ready,
        selected,selected_valid,selected_ready,);
    axis_capture_pack packer(clk,resetn,selected,selected_valid,selected_ready,
        packed_data,,packed_valid,packed_ready,);
    axis_capture_frame frame(clk,resetn,32'd1024,rate,packed_data,packed_valid,packed_ready,
        output_data,keep,output_valid,output_ready,last,peaks,18'd0,settings);
    always #5 clk=~clk;
    function automatic [7:0] code(input integer at,input integer channel);
        integer effective;
        begin
            effective=at/(divider?2*divider:1);
            code=((effective%128>=64)^channel)?180:100;
        end
    endfunction
    initial begin
        for(epoch=0;epoch<12;epoch=epoch+1)begin
            @(negedge clk);resetn=0;valid=0;
            repeat(3)@(negedge clk);
            case(epoch%6)0:divider=0;1:divider=1;2:divider=2;3:divider=5;4:divider=10;5:divider=20;endcase
            rate=divider?100000000/divider:200000000;
            settings=0;settings[31:0]=1|((epoch%2)<<1)|((epoch%2)<<2)|((epoch%4)<<4);
            settings[63:32]=epoch>=6?39*65536:0;
            settings[95:64]=140*65536;settings[127:96]=2*65536;
            settings[191:128]=32;settings[287:256]=epoch+1;
            cycle=0;sent=0;words=0;frames=0;hits=0;resetn=1;
            wait(frames==8);
            if(hits<4)$fatal(1,"not enough latched trigger headers at divider %d",divider);
        end
        passed=1;$display("PASS: decimation -> window statistics -> Wiener/FILTER -> first trigger, both channels, all FILTER levels, noise, raw/peak and stalls");$finish;
    end
    always @(negedge clk)if(resetn)begin
        cycle=cycle+1;valid=cycle%11!=0;
        data={code(sent*2+1,1),code(sent*2+1,0),code(sent*2,1),code(sent*2,0)};
        if(cycle>300000)$fatal(1,"timeout");
    end
    always @(posedge clk)if(resetn)begin
        if(valid&&adc_ready)sent=sent+1;
        if(output_valid&&output_ready)begin
            offset=words%136;
            if(offset==4)held_arm=output_data[63:32];
            if(offset==5)held_sample=output_data;
            if(offset==6)begin
                if(output_data[63:32]!=0)$fatal(1,"reserved bits");
                held_control=output_data[31:0];
            end
            if(offset==7)begin
                if(output_data[31:0]!=32'h31475254 || held_arm!=epoch+1 || held_control!=settings[31:0])
                    $fatal(1,"inconsistent TRG1 header");
                if(output_data[32])begin
                    if(held_sample!=64)$fatal(1,"first trigger moved: divider=%d FILTER=%d event=%d",divider,epoch%4,held_sample);
                    hits=hits+1;
                end else if(hits)$fatal(1,"latched trigger disappeared");
            end
            if(last!==(offset==135))$fatal(1,"TLAST");
            if(last)frames=frames+1;
            words=words+1;
        end
    end
endmodule
