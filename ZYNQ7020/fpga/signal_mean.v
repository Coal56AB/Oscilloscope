// Rounded division by the supported boxcar lengths, one numerator per clock.
// The input is bounded by length*255*65536. Radix-4 stages avoid a long
// combinational constant-divider path and do not consume DSP multipliers.
module signal_mean(
    input wire clk,resetn,valid,
    input wire [30:0] sum,
    input wire [1:0] level,
    output wire result_valid,
    output wire [23:0] result
);
    reg pv[0:16];
    reg [31:0] numerator[0:16];
    reg [23:0] quotient[0:16];
    reg [6:0] remainder[0:16],divisor[0:16];
    wire [6:0] length=level==0?7'd1:level==1?7'd5:level==2?7'd17:7'd65;
    assign result_valid=pv[16];assign result=quotient[16];
    always @(posedge clk)begin
        if(!resetn)pv[0]<=0;
        else begin
            pv[0]<=valid;
            if(valid)begin
                numerator[0]<={1'b0,sum}+(length>>1);quotient[0]<=0;
                remainder[0]<=0;divisor[0]<=length;
            end
        end
    end
    genvar stage;
    generate for(stage=0;stage<16;stage=stage+1)begin: divide
        reg [8:0] trial;
        reg [1:0] digit;
        integer bit_index;
        always @*begin
            trial={remainder[stage],numerator[stage][31:30]};digit=0;
            for(bit_index=1;bit_index>=0;bit_index=bit_index-1)
                if(trial>=({2'd0,divisor[stage]}<<bit_index))begin
                    trial=trial-({2'd0,divisor[stage]}<<bit_index);digit[bit_index]=1;
                end
        end
        always @(posedge clk)begin
            if(!resetn)pv[stage+1]<=0;
            else begin
                pv[stage+1]<=pv[stage];
                if(pv[stage])begin
                    numerator[stage+1]<={numerator[stage][29:0],2'd0};
                    quotient[stage+1]<={quotient[stage][21:0],digit};
                    remainder[stage+1]<=trial[6:0];divisor[stage+1]<=divisor[stage];
                end
            end
        end
    end endgenerate
endmodule
