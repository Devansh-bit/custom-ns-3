"use client";
import {Video, Headphones, CreditCard} from 'lucide-react'


export default function ClientApp() {
  return <div id="client-app">Client Application Wise
    <div className='flex'>
    <div id="video">
      <div id="examples" className='flex'>
        <Video/>
        <div>
          <span>Video</span>
          <div>Youtube, Google meet, Zoom, Instagram etc.</div>
        </div>
      </div>
      <div id="video-metrics">
      <div><span>Latency</span><span>65% Good</span></div>
        <div className='flex rounded-2xl'>
          <div className="bg-red-500/75">hello</div><div className='bg-amber-400/75'>meow</div><div className='bg-emerald-500/75'>hi</div>
        </div>
        <div>
        <div><span>Jitter</span><span>65% Good</span></div>
        <div className='flex'>
          <div className="bg-red-500/75">hello</div><div className='bg-amber-400/75'>meow</div><div className='bg-emerald-500/75'>hi</div>
        </div>
        </div>
        <div>
        <div><span>Packet Loss</span><span>65% Good</span></div>
        <div className='flex'>
          <div className="bg-red-500/75">hello</div><div className='bg-amber-400/75'>meow</div><div className='bg-emerald-500/75'>hi</div>
        </div>
        </div>
      </div>
    </div>


    <div id="audio">
    <div id="examples-audio" className='flex'>
        <Headphones/>
        <div>
          <span>Audio</span>
          <div>Whatsapp, Spotify, Audible, Apple Music etc.</div>
        </div>
      </div>
      <div id="metrics">
        <div>
        <div><span>Latency</span><span>65% Good</span></div>
        <div className='flex'>
          <div className="bg-red-500/75">hello</div><div className='bg-amber-400/75'>meow</div><div className='bg-emerald-500/75'>hi</div>
        </div>
        </div>
        <div>
        <div><span>Jitter</span><span>65% Good</span></div>
        <div className='flex'>
          <div className="bg-red-500/75">hello</div><div className='bg-amber-400/75'>meow</div><div className='bg-emerald-500/75'>hi</div>
        </div>
        </div>
        <div>
        <div><span>Packet Loss</span><span>65% Good</span></div>
          <div className='flex'>
          <div className="bg-red-500/75">hello</div><div className='bg-amber-400/75'>meow</div><div className='bg-emerald-500/75'>hi</div>
        </div></div>
      </div>
    </div>

    <div id="transaction">
    <div id="examples-transaction" className='flex'>
        <CreditCard/>
        <div>
          <span>Transaction</span>
          <div>Paypal, Stripe, Razorpay, Google Pay etc.</div>
        </div>
      </div>
      <div id="metrics">
        <div>
        <div><span>Latency</span><span>65% Good</span></div><div className='flex'>
          <div className="bg-red-500/75">hello</div><div className='bg-amber-400/75'>meow</div><div className='bg-emerald-500/75'>hi</div>
        </div></div>
        <div>
        <div><span>Jitter</span><span>65% Good</span></div>
        <div className='flex'>
          <div className="bg-red-500/75">hello</div><div className='bg-amber-400/75'>meow</div><div className='bg-emerald-500/75'>hi</div>
        </div>
        </div>
        <div>
        <div><span>Packet loss</span><span>65% Good</span></div>
        <div className='flex'>
          <div className="bg-red-500/75">hello</div><div className='bg-amber-400/75'>meow</div><div className='bg-emerald-500/75'>hi</div>
        </div>
        </div>
      </div>

    </div>
  </div>
  </div>;
}