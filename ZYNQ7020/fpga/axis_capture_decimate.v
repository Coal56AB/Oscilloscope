// Full rate keeps A/B. Every decimated point averages its complete interval;
// original min/max are retained over the two-point interval independently.
module axis_capture_decimate(
    (* X_INTERFACE_PARAMETER="XIL_INTERFACENAME clk, ASSOCIATED_BUSIF s_axis:m_axis, ASSOCIATED_RESET resetn" *)
    input wire clk, input wire resetn, input wire [31:0] group_divider,
    input wire [31:0] s_axis_tdata, input wire s_axis_tvalid,
    output wire s_axis_tready,
    output wire [31:0] m_axis_tdata, output wire m_axis_tvalid,
    input wire m_axis_tready,
    output wire peak_detect);
    reg [31:0] position;
    reg [15:0] first_pair;
    reg [28:0] sum0,sum1;
    reg [20:0] carry0,carry1;
    reg [7:0] low0, low1, high0, high1;
    reg [63:0] output_data;
    reg [1:0] output_words;
    reg [7:0] average_valid;
    reg [28:0] remainder0[0:7],remainder1[0:7];
    reg [7:0] quotient0[0:7],quotient1[0:7];
    reg [32:0] tags[0:7];
    wire last=position==((group_divider<<1)-1'b1);
    wire point_last=position==group_divider-1'b1 || last;
    wire space=output_words==0 || (output_words==1 && m_axis_tready);
    wire advance=!average_valid[7] || !tags[7][32] || space;
    wire [7:0] a0=s_axis_tdata[7:0], a1=s_axis_tdata[15:8];
    wire [7:0] b0=s_axis_tdata[23:16], b1=s_axis_tdata[31:24];
    // Production dividers are 1..1000000. Two ADC branches contribute to
    // each point: denominator <=2000000, sum <=510000000 (29 bits).
    wire [20:0] denominator={group_divider[19:0],1'b0};
    wire [28:0] total0=sum0+a0+b0,total1=sum1+a1+b1;
    wire [28:0] top_threshold={denominator,7'd0};
    wire [21:0] residual0={1'b0,carry0}+remainder0[7][20:0];
    wire [21:0] residual1={1'b0,carry1}+remainder1[7][20:0];
    wire round0=residual0>={1'b0,denominator};
    wire round1=residual1>={1'b0,denominator};
    wire [7:0] mean0=quotient0[7]+round0,mean1=quotient1[7]+round1;
    wire [7:0] min0=a0<b0?a0:b0, min1=a1<b1?a1:b1;
    wire [7:0] max0=a0>b0?a0:b0, max1=a1>b1?a1:b1;
    wire [7:0] next_low0=position==0 || min0<low0?min0:low0;
    wire [7:0] next_low1=position==0 || min1<low1?min1:low1;
    wire [7:0] next_high0=position==0 || max0>high0?max0:high0;
    wire [7:0] next_high1=position==0 || max1>high1?max1:high1;
    assign peak_detect=group_divider!=0;
    assign m_axis_tdata=output_data[31:0];
    assign m_axis_tvalid=output_words!=0;
    assign s_axis_tready=peak_detect?advance:space;
    integer k;
    always @(posedge clk) begin
        if (!resetn) begin
            position<=0;first_pair<=0;sum0<=0;sum1<=0;
            carry0<=0;carry1<=0;average_valid<=0;
            low0<=0;low1<=0;high0<=0;high1<=0;
            output_data<=0;output_words<=0;
        end else begin
            if (m_axis_tvalid && m_axis_tready) begin
                output_data<=output_data>>32;output_words<=output_words-1'b1;
            end
            if(peak_detect && advance)begin
                average_valid[0]<=s_axis_tvalid && point_last;
                if(s_axis_tvalid && point_last)begin
                    remainder0[0]<=total0>=top_threshold?total0-top_threshold:total0;
                    remainder1[0]<=total1>=top_threshold?total1-top_threshold:total1;
                    quotient0[0]<=total0>=top_threshold?8'h80:0;
                    quotient1[0]<=total1>=top_threshold?8'h80:0;
                    tags[0]<={last,next_high1,next_high0,next_low1,next_low0};
                end
                // Eight binary quotient digits, one carry chain per stage.
                // There is no variable combinational divider in the ADC path.
                for(k=1;k<8;k=k+1)begin
                    average_valid[k]<=average_valid[k-1];
                    if(average_valid[k-1])begin
                        remainder0[k]<=remainder0[k-1]>=(top_threshold>>k)?
                            remainder0[k-1]-(top_threshold>>k):remainder0[k-1];
                        remainder1[k]<=remainder1[k-1]>=(top_threshold>>k)?
                            remainder1[k-1]-(top_threshold>>k):remainder1[k-1];
                        quotient0[k]<=quotient0[k-1] |
                            (remainder0[k-1]>=(top_threshold>>k)?(8'h80>>k):8'd0);
                        quotient1[k]<=quotient1[k-1] |
                            (remainder1[k-1]>=(top_threshold>>k)?(8'h80>>k):8'd0);
                        tags[k]<=tags[k-1];
                    end
                end
                if(average_valid[7])begin
                    // Error feedback prevents a fractional ADC code from
                    // moving the DC baseline when the timebase changes.
                    carry0<=round0?residual0-denominator:residual0;
                    carry1<=round1?residual1-denominator:residual1;
                    if(tags[7][32])begin
                        output_data<={tags[7][31:0],mean1,mean0,first_pair};
                        output_words<=2;
                    end else first_pair<={mean1,mean0};
                end
            end
            if (s_axis_tvalid && s_axis_tready) begin
                if (!peak_detect) begin
                    output_data<={32'b0,s_axis_tdata};output_words<=1;
                end else begin
                    low0<=next_low0;low1<=next_low1;
                    high0<=next_high0;high1<=next_high1;
                    sum0<=point_last?29'd0:total0;sum1<=point_last?29'd0:total1;
                    if (last) begin
                        position<=0;
                    end else position<=position+1'b1;
                end
            end
        end
    end
endmodule
