// Exact integer statistics of centred 17-sample windows. Results for block B
// finish after the first eight samples of B+1, before the header of B+2.
module capture_window_stats(
    input wire clk,resetn,accept,peak_detect,
    input wire [63:0] data,
    input wire [31:0] position,
    input wire [63:0] sequence_id,
    input wire [31:0] previous_raw_bounds,
    output reg result_valid,
    output reg [63:0] result_sequence,
    output reg [31:0] result_raw_bounds,
    output reg [63:0] result_ch0,result_ch1,
    output wire group_valid,group_first,group_last,group_peak,
    output wire [63:0] group_sequence,
    output wire [103:0] group_sums,
    output wire [183:0] group_variances,
    output wire [63:0] group_centers
);
    reg av,bv,cv,dv,ev;
    reg [2:0] lanes_a,lanes_b;
    reg first_a,last_a,first_b,last_b,first_c,last_c,first_d,last_d,first_e,last_e;
    reg [63:0] seq_a,seq_b,seq_c,seq_d,seq_e;
    reg [31:0] raw_a,raw_b,raw_c,raw_d,raw_e;
    reg [4:0] filled;
    reg [7:0] codes[0:1][0:3],history[0:1][0:15];
    reg [15:0] squares[0:1][0:3],history_squares[0:1][0:15];
    reg [12:0] running_sum[0:1];
    reg [20:0] running_squares[0:1];
    reg [12:0] sums_b[0:1][0:3],sums_c[0:1][0:3];
    reg [20:0] square_sums_b[0:1][0:3];
    reg [22:0] variances_c[0:1][0:3];
    reg [7:0] centers_b[0:1][0:3],centers_c[0:1][0:3];
    reg [12:0] minimum_d[0:1],maximum_d[0:1],minimum[0:1],maximum[0:1];
    reg [22:0] variance_d[0:1],variance[0:1];
    reg [12:0] pair_minimum[0:1][0:1],pair_maximum[0:1][0:1];
    reg [22:0] pair_variance[0:1][0:1];
    reg pair_only_c,pair_only_d;
    function [12:0] min13;input [12:0] a,b;begin min13=a<b?a:b;end endfunction
    function [12:0] max13;input [12:0] a,b;begin max13=a>b?a:b;end endfunction
    function [22:0] max23;input [22:0] a,b;begin max23=a>b?a:b;end endfunction
    integer ch,k;
    reg [12:0] new_sum,old_sum,final_min,final_max;
    reg [20:0] new_squares,old_squares;
    reg [22:0] final_variance;
    wire [2:0] input_lanes=peak_detect?3'd2:3'd4;
    assign group_valid=cv;assign group_first=first_c;assign group_last=last_c;
    assign group_peak=pair_only_c;assign group_sequence=seq_c;
    genvar gc,gk;
    generate for(gc=0;gc<2;gc=gc+1)begin: channels
        for(gk=0;gk<4;gk=gk+1)begin: lanes
            assign group_sums[(gc*4+gk)*13+:13]=sums_c[gc][gk];
            assign group_variances[(gc*4+gk)*23+:23]=variances_c[gc][gk];
            assign group_centers[(gc*4+gk)*8+:8]=centers_c[gc][gk];
        end
    end endgenerate
    always @(posedge clk)begin
        if(!resetn)begin
            av<=0;bv<=0;cv<=0;dv<=0;ev<=0;filled<=0;
            result_valid<=0;result_sequence<=0;result_raw_bounds<=0;result_ch0<=0;result_ch1<=0;
            for(ch=0;ch<2;ch=ch+1)begin
                running_sum[ch]<=0;running_squares[ch]<=0;
                minimum[ch]<=13'h1fff;maximum[ch]<=0;variance[ch]<=0;
                for(k=0;k<16;k=k+1)begin history[ch][k]<=0;history_squares[ch][k]<=0;end
            end
        end else begin
            av<=accept;
            if(accept)begin
                lanes_a<=input_lanes;first_a<=position==8;
                last_a<=position==8-input_lanes && sequence_id!=0;
                seq_a<=sequence_id-1'b1;raw_a<=previous_raw_bounds;
                for(ch=0;ch<2;ch=ch+1)for(k=0;k<4;k=k+1)begin
                    codes[ch][k]<=data[k*16+ch*8+:8];
                    squares[ch][k]<=data[k*16+ch*8+:8]*data[k*16+ch*8+:8];
                end
            end
            bv<=av && filled==16;
            if(av)begin
                if(filled+lanes_a>=16)filled<=16;else filled<=filled+lanes_a;
                lanes_b<=lanes_a;first_b<=first_a;last_b<=last_a;seq_b<=seq_a;raw_b<=raw_a;
                for(ch=0;ch<2;ch=ch+1)begin
                    new_sum=0;old_sum=0;new_squares=0;old_squares=0;
                    for(k=0;k<4;k=k+1)begin
                        new_sum=new_sum+codes[ch][k];new_squares=new_squares+squares[ch][k];
                        sums_b[ch][k]<=running_sum[ch]+new_sum-old_sum;
                        centers_b[ch][k]<=history[ch][8+k];
                        square_sums_b[ch][k]<=running_squares[ch]+new_squares-old_squares;
                        old_sum=old_sum+history[ch][k];old_squares=old_squares+history_squares[ch][k];
                        if(k==lanes_a-1)begin
                            running_sum[ch]<=running_sum[ch]+new_sum-old_sum;
                            running_squares[ch]<=running_squares[ch]+new_squares-old_squares;
                        end
                    end
                    for(k=0;k<16;k=k+1)begin
                        if(k<16-lanes_a)begin
                            history[ch][k]<=history[ch][k+lanes_a];
                            history_squares[ch][k]<=history_squares[ch][k+lanes_a];
                        end else begin
                            history[ch][k]<=codes[ch][k-(16-lanes_a)];
                            history_squares[ch][k]<=squares[ch][k-(16-lanes_a)];
                        end
                    end
                end
            end
            cv<=bv;
            if(bv)begin
                first_c<=first_b;last_c<=last_b;seq_c<=seq_b;raw_c<=raw_b;
                pair_only_c<=lanes_b==2;
                for(ch=0;ch<2;ch=ch+1)for(k=0;k<4;k=k+1)begin
                    sums_c[ch][k]<=sums_b[ch][k];
                    centers_c[ch][k]<=centers_b[ch][k];
                    variances_c[ch][k]<=17*square_sums_b[ch][k]-sums_b[ch][k]*sums_b[ch][k];
                end
            end
            dv<=cv;
            if(cv)begin
                first_d<=first_c;last_d<=last_c;seq_d<=seq_c;raw_d<=raw_c;
                pair_only_d<=pair_only_c;
                for(ch=0;ch<2;ch=ch+1)for(k=0;k<2;k=k+1)begin
                    pair_minimum[ch][k]<=min13(sums_c[ch][k*2],sums_c[ch][k*2+1]);
                    pair_maximum[ch][k]<=max13(sums_c[ch][k*2],sums_c[ch][k*2+1]);
                    pair_variance[ch][k]<=max23(variances_c[ch][k*2],variances_c[ch][k*2+1]);
                end
            end
            ev<=dv;
            if(dv)begin
                first_e<=first_d;last_e<=last_d;seq_e<=seq_d;raw_e<=raw_d;
                for(ch=0;ch<2;ch=ch+1)begin
                    minimum_d[ch]<=pair_only_d?pair_minimum[ch][0]:min13(pair_minimum[ch][0],pair_minimum[ch][1]);
                    maximum_d[ch]<=pair_only_d?pair_maximum[ch][0]:max13(pair_maximum[ch][0],pair_maximum[ch][1]);
                    variance_d[ch]<=pair_only_d?pair_variance[ch][0]:max23(pair_variance[ch][0],pair_variance[ch][1]);
                end
            end
            if(ev)begin
                for(ch=0;ch<2;ch=ch+1)begin
                    final_min=first_e || minimum_d[ch]<minimum[ch]?minimum_d[ch]:minimum[ch];
                    final_max=first_e || maximum_d[ch]>maximum[ch]?maximum_d[ch]:maximum[ch];
                    final_variance=first_e || variance_d[ch]>variance[ch]?variance_d[ch]:variance[ch];
                    minimum[ch]<=final_min;maximum[ch]<=final_max;variance[ch]<=final_variance;
                    if(last_e)begin
                        // First block lacks the eight preceding samples.
                        result_valid<=seq_e!=0;result_sequence<=seq_e;result_raw_bounds<=raw_e;
                        if(ch==0)result_ch0<={15'd0,final_variance,final_max,final_min};
                        else result_ch1<={15'd0,final_variance,final_max,final_min};
                    end
                end
            end
        end
    end
endmodule
