import { Separator } from "@/components/ui/separator";

type Row = { ch: number; status: string; action: string };

const baseRows: Row[] = [
  { ch: 52, status: "Busy%", action: "Removed" },
  { ch: 56, status: "Busy%", action: "Removed" },
  { ch: 60, status: "Busy%", action: "Removed" },
  { ch: 64, status: "Busy%", action: "Removed" },
];

const extraRows: Row[] = Array.from({ length: 12 }, (_, i) => 100 + i * 4).map((ch) => ({
  ch,
  status: "Busy%",
  action: "Removed",
}));

const rows: Row[] = [...baseRows, ...extraRows];

export default function DFSBusyChannel() {
  return (
    <div className="w-full flex items-center justify-center">
      <div className="bg-white h-full rounded-lg shadow-sm border border-gray-200 p-8 w-full mr-5">
        {/* Header */}
        <h2 className="mb-6 font-bold">DFS Busy Channel</h2>



        <Separator />

        <div>
          <div className="grid grid-cols-3 items-center bg-cyan-200 h-14">
            <div className="text-center">Channel</div>
            <div className="text-center">Busy %</div>
            <div className="text-center">Status</div>
          </div>
        </div>
        <div className="overflow-y-hidden hover:overflow-y-scroll h-70 busy-channel">
          {rows.map((r, idx) => (
            <div key={r.ch} className="even:bg-cyan-100">
              <div className="grid grid-cols-3 py-4">
                <div className="text-center">{r.ch}</div>
                <div className="text-center">{r.status}</div>
                <div className="text-center">{r.action}</div>
              </div>
              {idx < rows.length - 1 && <Separator />}
            </div>
          ))}
        </div>


      </div>
    </div>
  );
}

