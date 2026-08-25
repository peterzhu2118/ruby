require "benchmark"

tbl = IDTable.new

Benchmark.bm do |x|
  x.report("add") do
    2.times { tbl.add(1_000_000) }
  end

  x.report("lookup") do
    10.times { tbl.lookup(1_000_000) }
  end
end
