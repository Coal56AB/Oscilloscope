`timescale 1ns/1ps
module axis_capture_average_tb;
    reg clk=0,resetn=0,valid=0,ready=0,passed=0;
    reg [31:0] divider=0,data=0;
    wire take,out_valid,peaks;
    wire [31:0] out_data;
    axis_capture_decimate dut(clk,resetn,divider,data,valid,take,out_data,out_valid,ready,peaks);
    always #5 clk=~clk;
    integer epoch,cycle,sent,received,target,position,written,points;
    integer total0,total1,residual0,residual1,low0,low1,high0,high1;
    integer code0,code1,a0,b0,a1,b1;
    reg [31:0] expected[0:8191];
    reg [15:0] first_pair;
    reg [31:0] random_state=32'hd4b591e3,held_data;
    reg held;
    real sum,squares,rms,previous_rms;
    function automatic [31:0] noise(input [31:0] x);
        reg [31:0] t;
        begin t=x^(x<<13);t=t^(t>>17);noise=t^(t<<5);end
    endfunction
    initial begin
        previous_rms=100;
        for(epoch=0;epoch<27;epoch=epoch+1)begin
            @(negedge clk);resetn=0;valid=0;ready=0;
            repeat(3)@(negedge clk);
            case(epoch<19?epoch:epoch-19)
                0:divider=1;1:divider=2;2:divider=5;3:divider=10;4:divider=20;
                5:divider=50;6:divider=100;7:divider=200;8:divider=500;
                9:divider=1000;10:divider=2000;11:divider=5000;12:divider=10000;
                13:divider=20000;14:divider=50000;15:divider=100000;
                16:divider=200000;17:divider=500000;18:divider=1000000;
            endcase
            sent=0;received=0;written=0;cycle=0;position=0;held=0;
            total0=0;total1=0;residual0=0;residual1=0;
            points=epoch<19?2:2048;target=points*divider;
            sum=0;squares=0;resetn=1;
            wait(received==points);
            if(epoch>=19)begin
                rms=$sqrt(squares/points-(sum/points)*(sum/points));
                $display("NOISE divider=%0d points=%0d rms_codes=%f",divider,points,rms);
                if(rms>previous_rms*1.1+0.15)$fatal(1,"noise increased after reducing sample rate");
                if(rms>13.5/$sqrt(2.0*divider)+0.6)$fatal(1,"noise outside averaging response");
                previous_rms=rms;
            end
        end
        passed=1;$display("PASS: all 19 decimation rates, maximum sums, DC, full-window means, extrema, stalls and noise response");$finish;
    end
    always @(negedge clk)if(resetn)begin
        cycle=cycle+1;valid=sent<target && cycle%7!=0;ready=cycle%11<8;
        a0=254;b0=255;a1=255;b1=255;
        if(epoch>=19)begin
            random_state=noise(random_state);a0=112+random_state[4:0];
            random_state=noise(random_state);b0=112+random_state[4:0];
            random_state=noise(random_state);a1=random_state[7:0];
            random_state=noise(random_state);b1=random_state[7:0];
        end
        data={8'(b1),8'(b0),8'(a1),8'(a0)};
        if(cycle>target*3+200)$fatal(1,"timeout at divider %d",divider);
    end
    always @(posedge clk)if(resetn)begin
        if(held && (!out_valid || out_data!==held_data))$fatal(1,"stalled output changed");
        held=out_valid&&!ready;held_data=out_data;
        if(valid&&take)begin
            if(position==0)begin low0=255;low1=255;high0=0;high1=0;end
            total0=total0+data[7:0]+data[23:16];total1=total1+data[15:8]+data[31:24];
            if(data[7:0]<low0)low0=data[7:0];if(data[23:16]<low0)low0=data[23:16];
            if(data[7:0]>high0)high0=data[7:0];if(data[23:16]>high0)high0=data[23:16];
            if(data[15:8]<low1)low1=data[15:8];if(data[31:24]<low1)low1=data[31:24];
            if(data[15:8]>high1)high1=data[15:8];if(data[31:24]>high1)high1=data[31:24];
            if(position==divider-1 || position==2*divider-1)begin
                total0=total0+residual0;total1=total1+residual1;
                code0=total0/(2*divider);code1=total1/(2*divider);
                residual0=total0%(2*divider);residual1=total1%(2*divider);
                if(position==divider-1)first_pair={8'(code1),8'(code0)};
                else begin
                    expected[written]={8'(code1),8'(code0),first_pair};
                    expected[written+1]={8'(high1),8'(high0),8'(low1),8'(low0)};written=written+2;
                end
                total0=0;total1=0;
            end
            position=position==2*divider-1?0:position+1;sent=sent+1;
        end
        if(out_valid&&ready)begin
            if(received>=written || out_data!==expected[received])
                $fatal(1,"divider=%d word=%d actual=%h expected=%h",divider,received,out_data,expected[received]);
            if(received%2==0)begin
                sum=sum+out_data[7:0]+out_data[23:16];
                squares=squares+integer'(out_data[7:0])*integer'(out_data[7:0])+integer'(out_data[23:16])*integer'(out_data[23:16]);
            end
            received=received+1;
        end
    end
endmodule
