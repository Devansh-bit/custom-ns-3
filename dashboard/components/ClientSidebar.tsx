import { Home, Users, Wifi, HelpCircle, LogOut } from 'lucide-react';

export function Sidebar() {
  return (
    <div className="w-[200px] h-screen bg-[#1a1f37] text-white flex flex-col">
      {/* Header */}
      <div className="p-4 border-b border-gray-700">
        <div className="flex items-center gap-3">
          <div className="w-10 h-10 bg-blue-500 rounded-lg flex items-center justify-center">
            <Wifi className="w-6 h-6" />
          </div>
          <div>
            <div className="text-sm">WiFi Manager</div>
            <div className="text-xs text-gray-400">Dashboard</div>
          </div>
        </div>
      </div>

      {/* Navigation */}
      <nav className="flex-1 p-4">
        <div className="space-y-2">
          <button className="w-full flex items-center gap-3 px-3 py-2 text-sm text-gray-300 hover:bg-gray-700/50 rounded">
            <Home className="w-4 h-4" />
            Home
          </button>
          
          <button className="w-full flex items-center gap-3 px-3 py-2 text-sm text-gray-300 hover:bg-gray-700/50 rounded">
            <Users className="w-4 h-4" />
            Client
          </button>
          
          <div className="pt-2">
            <button className="w-full flex items-center gap-3 px-3 py-2 text-sm bg-white text-gray-900 rounded mb-1">
              <Wifi className="w-4 h-4" />
              AP
            </button>
            
            <div className="ml-4 space-y-1">
              <button className="w-full text-left px-3 py-1.5 text-sm bg-gray-700/50 rounded">
                Wifi Auditorium
              </button>
              <button className="w-full text-left px-3 py-1.5 text-sm text-gray-400 hover:bg-gray-700/30 rounded">
                Wifi Audi. 2
              </button>
              <button className="w-full text-left px-3 py-1.5 text-sm text-gray-400 hover:bg-gray-700/30 rounded">
                Wifi Audi. 3
              </button>
              <button className="w-full text-left px-3 py-1.5 text-sm text-gray-400 hover:bg-gray-700/30 rounded">
                Wifi Audi. 4
              </button>
              <button className="w-full text-left px-3 py-1.5 text-sm text-gray-400 hover:bg-gray-700/30 rounded">
                Wifi Audi. 5
              </button>
              <button className="w-full text-left px-3 py-1.5 text-sm text-gray-400 hover:bg-gray-700/30 rounded">
                Wifi Audi. 6
              </button>
            </div>
          </div>
        </div>
      </nav>

      {/* Footer */}
      <div className="p-4 space-y-2">
        <button className="w-full flex items-center gap-3 px-3 py-2 text-sm text-gray-300 hover:bg-gray-700/50 rounded">
          <HelpCircle className="w-4 h-4" />
          Help & Information
        </button>
        <button className="w-full flex items-center gap-3 px-3 py-2 text-sm text-gray-300 hover:bg-gray-700/50 rounded">
          <LogOut className="w-4 h-4" />
          Log out
        </button>
      </div>
    </div>
  );
}
