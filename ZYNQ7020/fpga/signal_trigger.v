// One selected channel, from centred integer window statistics to an armed
// hardware trigger. Filtering and edge_select detection continue while disarmed;
// a new arm ID selects the first eligible crossing at/after minimum_sample.
module signal_trigger(
    input wire clk,resetn,valid,pair_only,
    input wire [63:0] first_sample,
    input wire [51:0] sums,
    input wire [91:0] variances,
    input wire [31:0] centers,
    input wire [31:0] noise_power,
    input wire [1:0] filter_level,edge_select,
    input wire signed [31:0] level,
    input wire [23:0] hysteresis,
    input wire enabled,
    input wire [31:0] arm_id,
    input wire [63:0] minimum_sample,holdoff_samples,
    output reg result_valid,
    output reg [31:0] result_arm_id,
    output reg [63:0] result_sample
);
    wire [95:0] denoised;
    wire wiener_valid;
    // Fixed-latency arithmetic has at most 32 outstanding groups. The tag
    // queue preserves their sample numbers through both full-rate and gaps.
    (* ram_style="distributed" *) reg [64:0] tags[0:63];
    reg [5:0] write_tag,read_tag;
    reg filtered_valid,filtered_pair;
    reg [63:0] filtered_first;
    reg [95:0] filtered_values;
    genvar lane;
    generate for(lane=0;lane<4;lane=lane+1)begin: wiener
        wire lane_valid;
        signal_wiener filter(clk,resetn,valid,centers[lane*8+:8],sums[lane*13+:13],
            variances[lane*23+:23],noise_power,lane_valid,denoised[lane*24+:24]);
        if(lane==0)assign wiener_valid=lane_valid;
    end endgenerate
    always @(posedge clk)begin
        if(!resetn)begin write_tag<=0;read_tag<=0;filtered_valid<=0;end
        else begin
            if(valid)begin tags[write_tag]<={pair_only,first_sample};write_tag<=write_tag+1'b1;end
            filtered_valid<=wiener_valid;
            if(wiener_valid)begin
                {filtered_pair,filtered_first}<=tags[read_tag];read_tag<=read_tag+1'b1;
                filtered_values<=denoised;
            end
        end
    end
    wire mean_valid,edge_valid;
    wire [3:0] mean_mask,edge_mask,rising;
    wire [63:0] mean_first;
    wire [95:0] mean_values,numerators,denominators;
    wire [255:0] bases;
    signal_boxcar boxcar(clk,resetn,filtered_valid,filtered_pair,filter_level,filtered_first,filtered_values,
        mean_valid,mean_mask,mean_first,mean_values);
    signal_edges crossings(clk,resetn,mean_valid,mean_mask,mean_first,mean_values,level,hysteresis,
        edge_valid,edge_mask,rising,bases,numerators,denominators);
    reg last_valid;
    reg [63:0] last_sample;
    reg av,bv,cv,enabled_a,enabled_b,enabled_c,last_valid_a;
    reg [31:0] arm_a,arm_b,arm_c;
    reg [63:0] minimum_a,holdoff_a,limit_b;
    reg [1:0] edge_a,edge_b;
    reg [3:0] mask_a,mask_b,rising_a,rising_b,eligible_c;
    reg [63:0] candidates_a[0:3],candidates_b[0:3],candidates_c[0:3];
    reg selected;
    integer k;
    always @(posedge clk)begin
        if(!resetn)begin
            av<=0;bv<=0;cv<=0;last_valid<=0;
            result_valid<=0;result_arm_id<=0;result_sample<=0;
        end else begin
            // Each candidate carries the arm that observed its confirmation.
            // Earlier stages from a previous arm can never satisfy a new arm.
            av<=edge_valid;
            if(edge_valid)begin
                arm_a<=arm_id;enabled_a<=enabled;edge_a<=edge_select;
                minimum_a<=minimum_sample;holdoff_a<=last_sample+holdoff_samples+1'b1;
                last_valid_a<=last_valid;mask_a<=edge_mask;rising_a<=rising;
                for(k=0;k<4;k=k+1)candidates_a[k]<=bases[k*64+:64]+
                    ({1'b0,numerators[k*24+:24],1'b0}>={2'd0,denominators[k*24+:24]});
            end
            bv<=av;
            if(av)begin
                arm_b<=arm_a;enabled_b<=enabled_a;edge_b<=edge_a;mask_b<=mask_a;rising_b<=rising_a;
                limit_b<=last_valid_a && holdoff_a>minimum_a?holdoff_a:minimum_a;
                for(k=0;k<4;k=k+1)candidates_b[k]<=candidates_a[k];
            end
            cv<=bv;
            if(bv)begin
                arm_c<=arm_b;enabled_c<=enabled_b;
                for(k=0;k<4;k=k+1)begin
                    eligible_c[k]<=mask_b[k] && candidates_b[k]>=limit_b &&
                        (edge_b==2 || (edge_b==0 && rising_b[k]) || (edge_b==1 && !rising_b[k]));
                    candidates_c[k]<=candidates_b[k];
                end
            end
            if(!enabled || result_arm_id!=arm_id)result_valid<=0;
            if(cv && enabled && enabled_c && arm_c==arm_id && !(result_valid && result_arm_id==arm_id))begin
                selected=0;
                for(k=0;k<4;k=k+1)if(!selected && eligible_c[k])begin
                    selected=1;result_valid<=1;result_arm_id<=arm_c;result_sample<=candidates_c[k];
                    last_valid<=1;last_sample<=candidates_c[k];
                end
            end
        end
    end
endmodule