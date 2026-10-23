    ZFIFO u_ZFIFO(.clk_i(clk_i),
        .rst_i(rst_i),
        .wr_en_i(wr_en_i),
        .rd_en_i(rd_en_i),
        .wr_data_i(wr_data_i),
        .full_o(full_o),
        .empty_o(empty_o),
        .rd_data_o(rd_data_o));
