`timescale 1ns/1ps
// Actual production AXIS IP: reset recovery, sustained flow and rearm.
module adc_stream_ip_tb;
    reg clk=0, sample_clk=0, resetn=0, enable=0, stall=0, passed=0;
    reg [7:0] a=0, b=0;
    wire clk_a, clk_b, stream_resetn, overflow;
    wire [31:0] adc_data, selected_data;
    reg [31:0] divider=0, rate=200000000;
    wire selected_valid, selected_ready;
    wire [63:0] packed_data, fifo_data, frame_data, data;
    wire adc_valid, adc_ready, packed_valid, packed_ready;
    wire fifo_valid, fifo_ready, frame_valid, frame_ready, frame_last;
    wire valid, last;
    wire [7:0] frame_keep, keep;
    integer cycles=0, written=0, read=0, words=0, blocks=0;
    reg [31:0] expected[0:65535];
    wire ready=!stall && cycles%17<14;
    ad9288_input adc(clk,sample_clk,resetn,enable,a,b,a+8'd64,b+8'd64,
        clk_a,clk_b,adc_data,adc_valid,adc_ready,stream_resetn,overflow);
    axis_capture_decimate decimate(sample_clk,stream_resetn,divider,
        adc_data,adc_valid,adc_ready,selected_data,selected_valid,selected_ready);
    adc_width_test width_ip(.aclk(sample_clk), .aresetn(stream_resetn),
        .s_axis_tdata(selected_data), .s_axis_tvalid(selected_valid), .s_axis_tready(selected_ready),
        .m_axis_tdata(packed_data), .m_axis_tvalid(packed_valid), .m_axis_tready(packed_ready));
    adc_fifo_test fifo_ip(.s_axis_aclk(sample_clk), .s_axis_aresetn(stream_resetn),
        .s_axis_tdata(packed_data), .s_axis_tvalid(packed_valid), .s_axis_tready(packed_ready),
        .m_axis_tdata(fifo_data), .m_axis_tvalid(fifo_valid), .m_axis_tready(fifo_ready));
    axis_capture_frame #(.BLOCK_BYTES(1024)) frame_ip(sample_clk,stream_resetn,32'd960,rate,
        fifo_data,fifo_valid,fifo_ready,frame_data,frame_keep,frame_valid,frame_ready,frame_last);
    adc_cdc_test cdc_ip(.s_axis_aclk(sample_clk), .s_axis_aresetn(stream_resetn),
        .m_axis_aclk(clk), .m_axis_aresetn(resetn),
        .s_axis_tdata(frame_data), .s_axis_tkeep(frame_keep), .s_axis_tlast(frame_last),
        .s_axis_tvalid(frame_valid), .s_axis_tready(frame_ready),
        .m_axis_tdata(data), .m_axis_tkeep(keep), .m_axis_tlast(last),
        .m_axis_tvalid(valid), .m_axis_tready(ready));
    always #5 clk=~clk;
    initial begin #4.125; forever #5 sample_clk=~sample_clk; end
    always @(posedge clk_a) a<=#6 a+1;
    always @(posedge clk_b) b<=#6 b+1;
    always @(posedge sample_clk) begin
        if(!stream_resetn) written=0;
        else if(selected_valid&&selected_ready) begin
            expected[written]=selected_data; written=written+1;
        end
        if(overflow&&!stall) $fatal(1,"ADC overflow with ready DMA at %t",$time);
    end
    always @(posedge clk) begin
        cycles<=cycles+1;
        if(!enable) begin read=0; words=0; blocks=0; end
        else if(valid&&ready) begin
            if(keep!==8'hff || last!==(words==127)) $fatal(1,"packet boundary");
            case(words)
                0: if(data!==64'h00000003_5043534f) $fatal(1,"magic");
                1: if(data!=={rate,32'd960}) $fatal(1,"format");
                2: if(data!==blocks) $fatal(1,"sequence");
                3: if(data!==blocks*480) $fatal(1,"sample counter");
                4,5,6,7: if(data!==0) $fatal(1,"reserved");
                default: begin
                    if(read+1>=written || data!=={expected[read+1],expected[read]})
                        $fatal(1,"data loss at %d",read);
                    read=read+2;
                end
            endcase
            if(words==127) begin words=0; blocks=blocks+1; end
            else words=words+1;
        end
    end
    initial begin
        #100; resetn=1;
        #1000; enable=1;
        #100000; if(blocks<30) $fatal(1,"no sustained DMA stream");
        stall=1; #100000;
        if(!overflow) $fatal(1,"missing overflow");
        enable=0; #1000; stall=0; #1000; enable=1;
        #100000; if(blocks<30) $fatal(1,"no stream after rearm");
        enable=0; #2000; divider=5;rate=20000000;#1000;enable=1;
        #100000; if(blocks<3) $fatal(1,"no decimated stream after reconfiguration");
        passed=1; $display("PASS: ADC through real width/FIFO/CDC IP, stalls, overflow and rearm");
        $finish;
    end
endmodule
