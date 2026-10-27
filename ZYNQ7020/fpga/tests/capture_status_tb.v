`timescale 1ns/1ps
module capture_status_tb;
    reg clk=0,overflow=0,passed=0;
    wire [31:0] legacy,analysis,filtered;
    always #5 clk=~clk;
    capture_status old_core(clk,overflow,legacy);
    capture_status #(.HAS_ANALYSIS(1)) measurement_core(clk,overflow,analysis);
    capture_status #(.HAS_ANALYSIS(1),.HAS_FILTER(1)) filter_core(clk,overflow,filtered);
    initial begin
        repeat(3)@(negedge clk);
        if(legacy!==32'hadc50002 || analysis!==32'hadc5000e || filtered!==32'hadc5001e)
            $fatal(1,"wrong feature advertisement");
        overflow=1;repeat(3)@(negedge clk);
        if(legacy!==32'hadc50003 || analysis!==32'hadc5000f || filtered!==32'hadc5001f)
            $fatal(1,"overflow changed feature bits");
        overflow=0;repeat(3)@(negedge clk);
        if(filtered!==32'hadc5001e)$fatal(1,"overflow did not clear");
        passed=1;$display("PASS: legacy, measurement and filter capabilities with synchronized overflow");$finish;
    end
endmodule
