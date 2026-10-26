// Necessary threshold-side tests for the same 17-point Wiener estimate as
// the software filter. Noise is an interval, so rounding can add candidates
// but cannot remove a resolved edge. No division and no payload modification.
module capture_trigger_probe(
    input wire clk,resetn,
    input wire [17:0] control,
    input wire group_valid,group_first,group_last,group_peak,
    input wire [63:0] group_sequence,
    input wire [103:0] group_sums,
    input wire [183:0] group_variances,
    input wire [63:0] group_centers,
    output reg result_valid,
    output reg [63:0] result_sequence,
    output reg [1:0] result_sides
);
    reg av,bv,cv;
    reg first_a,last_a,peak_a,first_b,last_b,peak_b,first_c,last_c,peak_c;
    reg [63:0] seq_a,seq_b,seq_c;
    reg [12:0] sums[0:3];
    reg [22:0] variances[0:3];
    reg [7:0] centers[0:3];
    reg signed [39:0] low_score[0:3],high_score[0:3],noise_low_score[0:3],noise_high_score[0:3];
    reg low_raw[0:3],high_raw[0:3],low_mean[0:3],high_mean[0:3];
    reg low_c[0:3],high_c[0:3],raw_possible[0:3],noise_low_full[0:3],noise_high_full[0:3];
    reg [1:0] sides;
    reg [20:0] noise_low,noise_high;
    wire [2:0] exponent=control[15:13];
    wire [4:0] mantissa=control[12:8];
    wire [12:0] power_low=exponent?({8'd0,1'b1,mantissa}<<(exponent-1)):mantissa;
    wire [12:0] power_high=exponent?(({8'd0,1'b1,mantissa}+1'b1)<<(exponent-1)):{8'd0,mantissa}+1'b1;
    wire signed [9:0] level_low=$signed({2'd0,control[7:0]})-10'sd1;
    wire signed [9:0] level_high=$signed({2'd0,control[7:0]})+10'sd1;
    integer k;
    reg [1:0] next_sides;
    reg signed [13:0] deviation,below,above;
    always @(posedge clk)begin
        if(!resetn)begin
            av<=0;bv<=0;cv<=0;sides<=0;result_valid<=0;result_sequence<=0;result_sides<=0;
            noise_low<=0;noise_high<=0;
        end else begin
            noise_low<=power_low*289;noise_high<=power_high*289;
            av<=group_valid;
            if(group_valid)begin
                first_a<=group_first;last_a<=group_last;peak_a<=group_peak;seq_a<=group_sequence;
                for(k=0;k<4;k=k+1)begin
                    sums[k]<=group_sums[(control[16]*4+k)*13+:13];
                    variances[k]<=group_variances[(control[16]*4+k)*23+:23];
                    centers[k]<=group_centers[(control[16]*4+k)*8+:8];
                end
            end
            bv<=av;
            if(av)begin
                first_b<=first_a;last_b<=last_a;peak_b<=peak_a;seq_b<=seq_a;
                for(k=0;k<4;k=k+1)begin
                    deviation=$signed({1'b0,centers[k]})*17-$signed({1'b0,sums[k]});
                    below=($signed({1'b0,centers[k]})-level_low)*17;
                    above=($signed({1'b0,centers[k]})-level_high)*17;
                    noise_low_score[k]<=$signed({1'b0,noise_low})*deviation;
                    noise_high_score[k]<=$signed({1'b0,noise_high})*deviation;
                    low_score[k]<=$signed({1'b0,variances[k]})*below;
                    high_score[k]<=$signed({1'b0,variances[k]})*above;
                    low_raw[k]<=$signed({1'b0,centers[k]})<=level_low;
                    high_raw[k]<=$signed({1'b0,centers[k]})>=level_high;
                    low_mean[k]<=$signed({1'b0,sums[k]})<=level_low*17;
                    high_mean[k]<=$signed({1'b0,sums[k]})>=level_high*17;
                    raw_possible[k]<=variances[k]>=9*noise_low;
                    noise_low_full[k]<=noise_low>=variances[k];
                    noise_high_full[k]<=noise_high>=variances[k];
                end
            end
            cv<=bv;
            if(bv)begin
                first_c<=first_b;last_c<=last_b;peak_c<=peak_b;seq_c<=seq_b;
                for(k=0;k<4;k=k+1)begin
                    if(control[15:8]==0)begin low_c[k]<=low_raw[k];high_c[k]<=high_raw[k];end
                    else if(control[15:8]==255)begin low_c[k]<=low_mean[k];high_c[k]<=high_mean[k];end
                    else begin
                        low_c[k]<=(raw_possible[k] && low_raw[k]) ||
                            (noise_low_full[k]?low_mean[k]:low_score[k]<=noise_low_score[k]) ||
                            (noise_high_full[k]?low_mean[k]:low_score[k]<=noise_high_score[k]);
                        high_c[k]<=(raw_possible[k] && high_raw[k]) ||
                            (noise_low_full[k]?high_mean[k]:high_score[k]>=noise_low_score[k]) ||
                            (noise_high_full[k]?high_mean[k]:high_score[k]>=noise_high_score[k]);
                    end
                end
            end
            if(cv)begin
                next_sides=first_c?2'b0:sides;
                for(k=0;k<4;k=k+1)if(k<(peak_c?2:4))next_sides=next_sides|{high_c[k],low_c[k]};
                sides<=next_sides;
                if(last_c)begin
                    result_valid<=seq_c!=0;result_sequence<=seq_c;result_sides<=next_sides;
                end
            end
        end
    end
endmodule
