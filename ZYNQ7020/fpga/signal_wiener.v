// One centred 17-point Wiener estimate per clock. Input statistics are exact
// integer ADC sums and 17*sum(x*x)-sum(x)*sum(x). Noise power is Q16.16;
// the result is unsigned Q8.16. The 24-bit division weight bounds the numeric
// error below two output LSBs; the 9*noise edge-preservation rule is unchanged.
module signal_wiener(
    input wire clk,resetn,
    input wire valid,
    input wire [7:0] center,
    input wire [12:0] sum,
    input wire [22:0] variance,
    input wire [31:0] noise_power,
    output reg result_valid,
    output reg [23:0] result
);
    localparam [1:0] RAW=0,MEAN=1,WIENER=2;
    reg av;
    reg [7:0] center_a;
    reg [12:0] sum_a;
    reg [22:0] variance_a;
    reg [40:0] noise_a;
    reg [44:0] edge_a;
    reg pv[0:24];
    reg [1:0] mode[0:24];
    reg [7:0] centers[0:24];
    reg [12:0] sums[0:24];
    reg [22:0] divisor[0:24],remainder[0:24];
    reg [23:0] dividend[0:24],quotient[0:24];
    wire [38:0] variance_scaled={variance_a,16'd0};
    wire [40:0] scaled_noise={9'd0,noise_power}*9'd289;
    genvar stage;
    generate for(stage=0;stage<24;stage=stage+1)begin: divide
        wire [23:0] trial={remainder[stage],dividend[stage][23]};
        wire subtract=trial>={1'b0,divisor[stage]};
        always @(posedge clk)begin
            if(!resetn)pv[stage+1]<=0;
            else begin
                pv[stage+1]<=pv[stage];
                if(pv[stage])begin
                    mode[stage+1]<=mode[stage];centers[stage+1]<=centers[stage];sums[stage+1]<=sums[stage];
                    divisor[stage+1]<=divisor[stage];
                    remainder[stage+1]<=subtract?trial-{1'b0,divisor[stage]}:trial;
                    dividend[stage+1]<={dividend[stage][22:0],1'b0};
                    quotient[stage+1]<={quotient[stage][22:0],subtract};
                end
            end
        end
    end endgenerate
    reg bv,cv,dv,ev,fv;
    reg signed [13:0] deviation_b;
    reg [23:0] weight_b;
    reg [12:0] raw_b,sum_b;
    reg [1:0] mode_b,mode_c;
    reg signed [38:0] correction_c;
    reg [36:0] raw_c,mean_c;
    reg [36:0] numerator_d;
    reg [28:0] rounded_e;
    // ceil(2^36/17) gives exact unsigned division for this 29-bit numerator.
    (* use_dsp="yes" *) reg [60:0] divided_f;
    always @(posedge clk)begin
        if(!resetn)begin
            av<=0;pv[0]<=0;bv<=0;cv<=0;dv<=0;ev<=0;fv<=0;result_valid<=0;result<=0;
        end else begin
            av<=valid;
            if(valid)begin
                center_a<=center;sum_a<=sum;variance_a<=variance;
                noise_a<=scaled_noise;edge_a<=scaled_noise*4'd9;
            end
            pv[0]<=av;
            if(av)begin
                mode[0]<=noise_a==0 || variance_scaled>=edge_a?RAW:
                    variance_scaled<=noise_a?MEAN:WIENER;
                centers[0]<=center_a;sums[0]<=sum_a;
                divisor[0]<=variance_a;
                // Only the WIENER branch needs the quotient. Its high part is
                // strictly below the divisor; the two upper bits are zero.
                remainder[0]<=variance_scaled>noise_a?noise_a[38:16]:23'd0;
                dividend[0]<={noise_a[15:0],8'd0};quotient[0]<=0;
            end
            bv<=pv[24];
            if(pv[24])begin
                deviation_b<=$signed({1'b0,centers[24]})*14'sd17-$signed({1'b0,sums[24]});
                weight_b<=quotient[24];raw_b<=centers[24]*5'd17;sum_b<=sums[24];mode_b<=mode[24];
            end
            cv<=bv;
            if(bv)begin
                correction_c<=$signed({1'b0,weight_b})*deviation_b;
                raw_c<={raw_b,24'd0};mean_c<={sum_b,24'd0};mode_c<=mode_b;
            end
            dv<=cv;
            if(cv)numerator_d<=mode_c==RAW?raw_c:mode_c==MEAN?mean_c:
                $signed({2'd0,raw_c})-correction_c;
            ev<=dv;
            if(dv)rounded_e<=(numerator_d+37'd2176)>>8;
            fv<=ev;
            if(ev)divided_f<=rounded_e*32'hf0f0f0f1;
            result_valid<=fv;
            if(fv)result<=divided_f[59:36];
        end
    end
endmodule
