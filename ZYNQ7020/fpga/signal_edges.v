// Ordered Schmitt crossings of four Q8.16 samples per clock. Confirmation at
// the opposite hysteresis boundary keeps the original level bracket, even
// after a long stay inside the band. Missing sample indices reset the state.
// A crossing is represented exactly by base + numerator/denominator, so the
// trigger and period/duty measurements can share the same event stream.
module signal_edges(
    input wire clk,resetn,valid,
    input wire [3:0] mask,
    input wire [63:0] first_sample,
    input wire [95:0] values,
    input wire signed [31:0] level,
    input wire [23:0] hysteresis,
    output reg result_valid,
    output reg [3:0] result_mask,result_rising,
    output reg [255:0] result_bases,
    output reg [95:0] result_numerators,result_denominators
);
    reg input_seen,av,zero_a;
    reg [23:0] input_previous;
    reg [63:0] input_next;
    reg [3:0] mask_a,connected_a,rising_a,falling_a;
    reg signed [1:0] sides_a[0:3];
    reg [63:0] bases_a[0:3];
    reg [23:0] numerators_a[0:3],denominators_a[0:3];
    reg signed [1:0] side;
    reg pending;
    reg [63:0] pending_base;
    reg [23:0] pending_numerator,pending_denominator;
    reg previous_seen;
    reg [23:0] previous_value,value;
    reg [63:0] next_index,index;
    reg signed [32:0] signed_value,signed_previous;
    wire signed [32:0] low=$signed(level)-$signed({9'd0,hysteresis});
    wire signed [32:0] high=$signed(level)+$signed({9'd0,hysteresis});
    reg signed [1:0] next_side;
    reg next_pending,emit;
    reg [63:0] next_base;
    reg [23:0] next_numerator,next_denominator;
    integer k;
    always @(posedge clk)begin
        if(!resetn)begin
            input_seen<=0;input_previous<=0;input_next<=0;av<=0;
            side<=0;pending<=0;result_valid<=0;result_mask<=0;
            result_rising<=0;result_bases<=0;result_numerators<=0;result_denominators<=0;
        end else begin
            av<=valid;
            if(valid)begin
                mask_a<=mask;zero_a<=hysteresis==0;
                previous_seen=input_seen;previous_value=input_previous;next_index=input_next;
                for(k=0;k<4;k=k+1)if(mask[k])begin
                    value=values[k*24+:24];index=first_sample+k;
                    signed_value=$signed({9'd0,value});signed_previous=$signed({9'd0,previous_value});
                    connected_a[k]<=previous_seen && index==next_index;
                    sides_a[k]<=signed_value<=low?-2'sd1:signed_value>=high?2'sd1:2'sd0;
                    rising_a[k]<=signed_previous<=$signed(level) && signed_value>$signed(level);
                    falling_a[k]<=signed_previous>=$signed(level) && signed_value<$signed(level);
                    bases_a[k]<=index-1'b1;
                    numerators_a[k]<=signed_value>signed_previous?$signed(level)-signed_previous:signed_previous-$signed(level);
                    denominators_a[k]<=value>previous_value?value-previous_value:previous_value-value;
                    previous_seen=1;previous_value=value;next_index=index+1'b1;
                end
                input_seen<=previous_seen;input_previous<=previous_value;input_next<=next_index;
            end
            result_valid<=av;
            if(av)begin
                next_side=side;next_pending=pending;
                next_base=pending_base;next_numerator=pending_numerator;next_denominator=pending_denominator;
                result_mask<=0;result_rising<=0;
                for(k=0;k<4;k=k+1)if(mask_a[k])begin
                    emit=0;
                    if(!connected_a[k])begin next_side=sides_a[k];next_pending=0;end
                    else if(zero_a)begin
                        emit=rising_a[k] || falling_a[k];
                        next_side=rising_a[k]?2'sd1:-2'sd1;
                        next_base=bases_a[k];next_numerator=numerators_a[k];next_denominator=denominators_a[k];
                        next_pending=0;
                    end else begin
                        if(!next_pending && ((next_side<0 && rising_a[k]) || (next_side>0 && falling_a[k])))begin
                            next_pending=1;next_base=bases_a[k];
                            next_numerator=numerators_a[k];next_denominator=denominators_a[k];
                        end
                        if(sides_a[k]!=0)begin
                            emit=next_side!=0 && next_side!=sides_a[k] && next_pending;
                            next_side=sides_a[k];next_pending=0;
                        end
                    end
                    if(emit)begin
                        result_mask[k]<=1;result_rising[k]<=next_side>0;
                        result_bases[k*64+:64]<=next_base;
                        result_numerators[k*24+:24]<=next_numerator;
                        result_denominators[k*24+:24]<=next_denominator;
                    end
                end
                side<=next_side;pending<=next_pending;
                pending_base<=next_base;pending_numerator<=next_numerator;pending_denominator<=next_denominator;
            end
        end
    end
endmodule
