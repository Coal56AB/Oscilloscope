// Centred 1/5/17/65-point mean of Q8.16 samples. A group contains two or
// four successive values of one channel. Configuration is held until reset.
// Outputs without complete left context are masked; their absolute sample
// numbers are delayed by the radius, independently of pipeline latency.
module signal_boxcar(
    input wire clk,resetn,valid,pair_only,
    input wire [1:0] level,
    input wire [63:0] first_sample,
    input wire [95:0] values,
    output reg result_valid,
    output reg [3:0] result_mask,
    output reg [63:0] result_first_sample,
    output reg [95:0] result_values
);
    reg [23:0] history[0:64];
    reg [30:0] running;
    reg [6:0] filled;
    wire [2:0] lanes=pair_only?3'd2:3'd4;
    wire [6:0] length=level==0?7'd1:level==1?7'd5:level==2?7'd17:7'd65;
    wire [5:0] radius=(length-1'b1)>>1;
    reg [23:0] outgoing[0:3];
    reg signed [24:0] delta[0:3];
    reg signed [24:0] delta_a[0:3];
    reg signed [26:0] prefix[0:3];
    reg [30:0] next_sum[0:3],sums_a[0:3];
    reg av,bv;
    reg [2:0] lanes_a;
    reg [3:0] mask_a,mask_b;
    reg [63:0] first_a,first_b;
    reg [1:0] level_a,level_b;
    wire [23:0] means[0:3];
    wire mean_valid;
    reg [63:0] first_pipeline[0:16];
    reg [3:0] mask_pipeline[0:16];
    genvar mean_lane;
    generate for(mean_lane=0;mean_lane<4;mean_lane=mean_lane+1)begin: mean
        wire lane_valid;
        signal_mean divide_mean(clk,resetn,bv,sums_a[mean_lane],level_b,lane_valid,means[mean_lane]);
        if(mean_lane==0)assign mean_valid=lane_valid;
    end endgenerate
    integer k,lane;
    always @*begin
        for(lane=0;lane<4;lane=lane+1)begin
            case(level)
                0:outgoing[lane]=lane==0?history[64]:values[(lane-1)*24+:24];
                1:outgoing[lane]=history[60+lane];
                2:outgoing[lane]=history[48+lane];
                default:outgoing[lane]=history[lane];
            endcase
            delta[lane]=$signed({1'b0,values[lane*24+:24]})-$signed({1'b0,outgoing[lane]});
        end
        prefix[0]=delta_a[0];prefix[1]=$signed(delta_a[0])+$signed(delta_a[1]);
        prefix[2]=prefix[1]+$signed(delta_a[2]);
        prefix[3]=prefix[1]+($signed(delta_a[2])+$signed(delta_a[3]));
        for(lane=0;lane<4;lane=lane+1)next_sum[lane]=$signed({1'b0,running})+$signed(prefix[lane]);
    end
    always @(posedge clk)begin
        if(!resetn)begin
            running<=0;filled<=0;av<=0;bv<=0;result_valid<=0;result_mask<=0;
            result_first_sample<=0;result_values<=0;
            for(k=0;k<65;k=k+1)history[k]<=0;
        end else begin
            av<=valid;
            if(valid)begin
                level_a<=level;first_a<=first_sample-radius;lanes_a<=lanes;
                if(filled+lanes>=65)filled<=65;else filled<=filled+lanes;
                for(k=0;k<4;k=k+1)begin
                    delta_a[k]<=delta[k];
                    mask_a[k]<=k<lanes && filled+k>=length-1;
                end
                for(k=0;k<65;k=k+1)
                    if(k<65-lanes)history[k]<=history[k+lanes];
                    else history[k]<=values[(k-(65-lanes))*24+:24];
            end
            bv<=av;
            if(av)begin
                running<=next_sum[lanes_a-1];
                level_b<=level_a;first_b<=first_a;mask_b<=mask_a;
                for(k=0;k<4;k=k+1)sums_a[k]<=next_sum[k];
            end
            first_pipeline[0]<=first_b;mask_pipeline[0]<=mask_b;
            for(k=0;k<16;k=k+1)begin
                first_pipeline[k+1]<=first_pipeline[k];mask_pipeline[k+1]<=mask_pipeline[k];
            end
            result_valid<=mean_valid;
            if(mean_valid)begin
                result_mask<=mask_pipeline[16];result_first_sample<=first_pipeline[16];
                for(k=0;k<4;k=k+1)result_values[k*24+:24]<=means[k];
            end
        end
    end
endmodule
