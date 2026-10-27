// AXI-Lite staging registers with atomic transfer into the ADC clock domain.
// Writes to staging are independent of a pending commit. The frozen transfer
// remains unchanged until the sample domain acknowledges it.
module capture_dsp_config(
    (* X_INTERFACE_PARAMETER="XIL_INTERFACENAME s_axi_aclk, ASSOCIATED_BUSIF s_axi, ASSOCIATED_RESET s_axi_aresetn" *)
    input wire s_axi_aclk,input wire s_axi_aresetn,
    input wire [7:0] s_axi_awaddr,input wire [2:0] s_axi_awprot,
    input wire s_axi_awvalid,output wire s_axi_awready,
    input wire [31:0] s_axi_wdata,input wire [3:0] s_axi_wstrb,
    input wire s_axi_wvalid,output wire s_axi_wready,
    output reg [1:0] s_axi_bresp,output reg s_axi_bvalid,input wire s_axi_bready,
    input wire [7:0] s_axi_araddr,input wire [2:0] s_axi_arprot,
    input wire s_axi_arvalid,output wire s_axi_arready,
    output reg [31:0] s_axi_rdata,output reg [1:0] s_axi_rresp,
    output reg s_axi_rvalid,input wire s_axi_rready,
    input wire sample_clk,sample_resetn,
    output reg [287:0] dsp_settings,
    output reg update
);
    localparam [31:0] ID=32'h31505344;
    reg address_pending,data_pending;
    reg [7:0] address;
    reg [31:0] data;
    reg [3:0] strobe;
    reg [31:0] staging[0:7];
    reg [287:0] transfer;
    reg request,acknowledge;
    (* ASYNC_REG="TRUE" *) reg ack_meta,ack_sync,request_meta,request_sync;
    wire busy=request!=ack_sync;
    assign s_axi_awready=!address_pending && !s_axi_bvalid;
    assign s_axi_wready=!data_pending && !s_axi_bvalid;
    assign s_axi_arready=!s_axi_rvalid;
    integer word,byte_index;
    always @(posedge s_axi_aclk)begin
        if(!s_axi_aresetn)begin
            address_pending<=0;data_pending<=0;s_axi_bvalid<=0;s_axi_bresp<=0;
            s_axi_rvalid<=0;s_axi_rresp<=0;s_axi_rdata<=0;
            transfer<=0;request<=0;ack_meta<=0;ack_sync<=0;
            for(word=0;word<8;word=word+1)staging[word]<=0;
        end else begin
            ack_meta<=acknowledge;ack_sync<=ack_meta;
            if(s_axi_awvalid && s_axi_awready)begin address_pending<=1;address<=s_axi_awaddr;end
            if(s_axi_wvalid && s_axi_wready)begin data_pending<=1;data<=s_axi_wdata;strobe<=s_axi_wstrb;end
            if(s_axi_bvalid && s_axi_bready)s_axi_bvalid<=0;
            if(address_pending && data_pending && !s_axi_bvalid)begin
                address_pending<=0;data_pending<=0;s_axi_bvalid<=1;s_axi_bresp<=0;
                if(address==8 && strobe==15 && !busy)begin
                    for(word=0;word<8;word=word+1)transfer[word*32+:32]<=staging[word];
                    transfer[287:256]<=data;request<=!request;
                end else if(address>=16 && address<48 && address[1:0]==0)begin
                    for(byte_index=0;byte_index<4;byte_index=byte_index+1)
                        if(strobe[byte_index])staging[(address-16)>>2][byte_index*8+:8]<=data[byte_index*8+:8];
                end else s_axi_bresp<=2'b10;
            end
            if(s_axi_rvalid && s_axi_rready)s_axi_rvalid<=0;
            if(s_axi_arvalid && s_axi_arready)begin
                s_axi_rvalid<=1;s_axi_rresp<=0;
                case(s_axi_araddr)
                    0:s_axi_rdata<=ID;
                    4:s_axi_rdata<={31'd0,busy};
                    12:s_axi_rdata<=busy?32'd0:transfer[287:256];
                    default:if(s_axi_araddr>=16 && s_axi_araddr<48 && s_axi_araddr[1:0]==0)
                        s_axi_rdata<=staging[(s_axi_araddr-16)>>2];
                    else begin s_axi_rdata<=0;s_axi_rresp<=2'b10;end
                endcase
            end
        end
    end
    always @(posedge sample_clk)begin
        if(!sample_resetn)begin
            request_meta<=0;request_sync<=0;acknowledge<=0;dsp_settings<=0;update<=0;
        end else begin
            request_meta<=request;request_sync<=request_meta;update<=0;
            if(request_sync!=acknowledge)begin
                dsp_settings<=transfer;acknowledge<=request_sync;update<=1;
            end
        end
    end
endmodule
