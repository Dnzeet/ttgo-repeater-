// Web UI repeater. Satu halaman, tanpa framework, tanpa sumber daya eksternal.
#pragma once
#include <Arduino.h>

static const char WEBUI_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="id">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Repeater</title>
<style>
:root{color-scheme:dark;--bg:#0e1311;--line:#26302b;--txt:#dde3dd;--dim:#7c8a82;--amb:#ffb020;--ok:#5bd18b;--err:#e5604d;--mono:ui-monospace,"SF Mono",Menlo,Consolas,monospace}
*{box-sizing:border-box}
html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--bg);color:var(--txt);font:15px/1.5 Inter,system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;font-variant-numeric:tabular-nums}
main{max-width:620px;margin:0 auto;padding:30px 18px 64px}
h1{font-size:25px;line-height:1.2;font-weight:600;letter-spacing:-.015em;margin:0 0 6px;overflow-wrap:anywhere}
h2{font-size:15px;font-weight:600;margin:0 0 12px}
.sub{margin:0 0 22px;color:var(--dim);min-height:1.5em}
.dot{display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:9px;background:var(--dim);vertical-align:2px}
.dot.online{background:var(--ok)}.dot.connecting{background:var(--amb)}
canvas{display:block;width:100%;height:118px}
.rates{display:flex;gap:28px;margin:12px 0 0;padding:0}
.rates div{display:block}
.rates dt{font-size:13px;color:var(--dim)}
.rates dd{margin:0;font-size:19px;font-weight:600}
.rates .dn dd{color:var(--amb)}
section{margin-top:38px;padding-top:18px;border-top:1px solid var(--line)}
.hint{margin:-4px 0 12px;color:var(--dim);font-size:14px}
.empty{color:var(--dim);margin:0}
.cl .r{display:grid;grid-template-columns:1fr auto auto;gap:14px;align-items:center;padding:9px 0;border-bottom:1px solid var(--line)}
.cl .r:last-child{border-bottom:0}
.m{font-family:var(--mono);font-size:14px}.dim{color:var(--dim)}
svg{vertical-align:-1px;margin-right:6px}
ul{list-style:none;margin:0 0 16px;padding:0}
li button.n{all:unset;box-sizing:border-box;display:grid;grid-template-columns:1fr auto auto;gap:12px;align-items:center;width:100%;padding:9px 6px;border-bottom:1px solid var(--line);cursor:pointer}
li button.n:hover{background:#151c19}
li button.n:focus-visible{outline:2px solid var(--amb);outline-offset:-2px}
label{display:block;margin:0 0 14px;font-size:14px;color:var(--dim)}
input[type=text],input[type=password],select{display:block;width:100%;margin-top:5px;padding:10px 11px;font:inherit;color:var(--txt);background:transparent;border:1px solid var(--line);border-radius:3px}
select{background:var(--bg)}
input:focus-visible,select:focus-visible,button:focus-visible{outline:2px solid var(--amb);outline-offset:1px}
label.chk{display:flex;align-items:center;gap:9px;color:var(--txt)}
input[type=checkbox]{width:17px;height:17px;accent-color:var(--amb);margin:0}
.row{display:flex;gap:10px;flex-wrap:wrap}
button{font:inherit;font-weight:600;padding:10px 16px;border-radius:3px;border:1px solid var(--amb);background:var(--amb);color:#1a1405;cursor:pointer}
button.g{background:transparent;color:var(--txt);border-color:var(--line)}
button.g:hover{border-color:var(--dim)}
button.d{background:transparent;color:var(--err);border-color:var(--line)}
button:disabled{opacity:.5;cursor:default}
.two{display:grid;grid-template-columns:1fr 1fr;gap:14px}
footer{margin-top:44px;color:var(--dim);font-size:13px}
#toast{position:fixed;left:50%;bottom:20px;transform:translate(-50%,12px);max-width:calc(100% - 36px);padding:10px 16px;background:var(--amb);color:#1a1405;font-weight:600;border-radius:3px;opacity:0;pointer-events:none;transition:opacity .15s,transform .15s}
#toast.on{opacity:1;transform:translate(-50%,0)}
@media (prefers-reduced-motion:reduce){#toast{transition:none}}
@media (max-width:420px){.two{grid-template-columns:1fr}}
</style>
</head>
<body>
<main>
<h1 id="hl"><span class="dot"></span>Memuat</h1>
<p class="sub" id="sl"></p>
<canvas id="g" aria-label="Grafik trafik 1 menit terakhir"></canvas>
<dl class="rates">
<div class="dn"><dt>Unduh</dt><dd id="rd">0</dd></div>
<div><dt>Unggah</dt><dd id="ru">0</dd></div>
<div><dt>Total</dt><dd id="rt">0</dd></div>
</dl>

<section>
<h2>Perangkat yang memakai repeater</h2>
<div class="cl" id="cl"></div>
</section>

<section>
<h2>Jaringan sumber</h2>
<p class="hint">WiFi yang diperkuat oleh repeater ini.</p>
<div class="row" style="margin-bottom:12px"><button class="g" id="bs" type="button">Cari jaringan</button></div>
<ul id="nl"></ul>
<label>Nama WiFi<input type="text" id="ss" maxlength="32" autocomplete="off" autocapitalize="off" spellcheck="false"></label>
<label>Sandi<input type="password" id="sp" maxlength="63" autocomplete="off"></label>
<label class="chk"><input type="checkbox" id="sh"> Tampilkan sandi</label>
<button id="bc" type="button" style="margin-top:6px">Sambungkan</button>
</section>

<section>
<h2>Hotspot repeater</h2>
<p class="hint">WiFi baru yang dipancarkan untuk perangkatmu.</p>
<label>Nama hotspot<input type="text" id="as" maxlength="32" autocomplete="off" autocapitalize="off" spellcheck="false"></label>
<label>Sandi baru<input type="password" id="ap" maxlength="63" autocomplete="new-password" placeholder="Kosongkan agar sandi tidak berubah"></label>
<label class="chk"><input type="checkbox" id="ao"> Tanpa sandi (terbuka untuk siapa saja)</label>
<button id="ab" type="button" style="margin-top:6px">Simpan hotspot</button>
</section>

<section>
<h2>Layar</h2>
<div class="two">
<label>Kecerahan<select id="br"><option value="0">10%</option><option value="1">25%</option><option value="2">50%</option><option value="3">75%</option><option value="4">100%</option></select></label>
<label>Redup otomatis<select id="dm"><option value="0">Tidak</option><option value="1">15 detik</option><option value="2">30 detik</option><option value="3">1 menit</option><option value="4">5 menit</option></select></label>
</div>
<label class="chk"><input type="checkbox" id="fl"> Putar layar 180 derajat</label>
</section>

<section>
<h2>Perangkat</h2>
<div class="row">
<button class="g" id="rb" type="button">Mulai ulang</button>
<button class="d" id="fg" type="button">Lupakan jaringan sumber</button>
</div>
<footer id="ft"></footer>
</section>
</main>
<div id="toast" role="status"></div>
<script>
const $=s=>document.querySelector(s);
const esc=s=>String(s).replace(/[&<>"']/g,c=>'&#'+c.charCodeAt(0)+';');
const rate=b=>{const t=b*8;return t>=1e6?(t/1e6).toFixed(1)+' Mb/s':Math.round(t/1e3)+' kb/s'};
const size=kb=>kb>=1048576?(kb/1048576).toFixed(2)+' GB':kb>=1024?(kb/1024).toFixed(1)+' MB':kb+' KB';
const span=s=>{const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return(d?d+' hari ':'')+h+' jam '+m+' menit'};
const bars=r=>{const l=r>-55?4:r>-65?3:r>-75?2:1;let s='<svg width="16" height="12" viewBox="0 0 16 12" aria-hidden="true">';for(let i=0;i<4;i++){const h=3+i*3;s+='<rect x="'+i*4+'" y="'+(12-h)+'" width="3" height="'+h+'" fill="'+(i<l?'#dde3dd':'#26302b')+'"/>'}return s+'</svg>'};
let toastT,nets=[],primed=false;
function toast(m){const t=$('#toast');t.textContent=m;t.classList.add('on');clearTimeout(toastT);toastT=setTimeout(()=>t.classList.remove('on'),3400)}
async function post(path,data){
  try{
    const r=await fetch(path,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)});
    const j=await r.json();toast(j.msg||(j.ok?'Tersimpan':'Gagal'));return j
  }catch(e){toast('Koneksi terputus. Sambungkan lagi ke hotspot repeater.')}
}
function draw(hd,hu){
  const c=$('#g'),x=c.getContext('2d'),k=window.devicePixelRatio||1,w=c.clientWidth,h=c.clientHeight;
  if(c.width!==Math.round(w*k)){c.width=Math.round(w*k);c.height=Math.round(h*k)}
  x.setTransform(k,0,0,k,0,0);x.clearRect(0,0,w,h);
  const n=hd.length,mx=Math.max(20000,...hd,...hu),bw=w/n,H=h-8;
  x.fillStyle='#ffb020';
  hd.forEach((v,i)=>{const bh=v/mx*H;x.fillRect(i*bw,h-1-bh,Math.max(bw-1,1),Math.max(bh,v?1:0))});
  x.fillStyle='#26302b';x.fillRect(0,h-1,w,1);
  x.strokeStyle='#dde3dd';x.lineWidth=1.5;x.lineJoin='round';x.beginPath();
  hu.forEach((v,i)=>{const y=h-2-v/mx*H;i?x.lineTo(i*bw+bw/2,y):x.moveTo(i*bw+bw/2,y)});x.stroke();
  x.fillStyle='#7c8a82';x.font='12px system-ui,sans-serif';x.textAlign='right';x.fillText(rate(mx),w-2,11);
}
function paint(s){
  const st=s.sta,on=st.state==='online';
  $('#hl').innerHTML='<span class="dot '+st.state+'"></span>'+esc(on?'Tersambung ke '+st.ssid:st.state==='connecting'?'Menyambung ke '+st.ssid:'Belum ada jaringan sumber');
  $('#sl').textContent=on?s.ap.clients.length+' perangkat memakai repeater. Sinyal '+st.rssi+' dBm di kanal '+st.ch+', alamat '+st.ip+'.':st.state==='connecting'?(st.reason||'Mencari jaringan...'):'Pilih jaringan di bagian bawah untuk memulai.';
  $('#rd').textContent=rate(s.net.dn);$('#ru').textContent=rate(s.net.up);$('#rt').textContent=size(s.net.dnT+s.net.upT);
  draw(s.net.hd,s.net.hu);
  const cl=s.ap.clients;
  $('#cl').innerHTML=cl.length?cl.map(c=>'<div class="r"><span class="m">'+esc(c.ip||'menunggu IP')+'</span><span class="m dim">'+esc(c.mac)+'</span><span>'+bars(c.rssi)+c.rssi+'</span></div>').join(''):'<p class="empty">Belum ada perangkat. Sambungkan ke WiFi &ldquo;'+esc(s.ap.ssid)+'&rdquo;.</p>';
  const bat=s.bat>0?(s.bat/1000).toFixed(2)+' V':'USB';
  $('#ft').textContent='Firmware '+s.fw+'. Aktif '+span(s.up)+'. Memori bebas '+Math.round(s.heap/1024)+' KB. Daya '+bat+'.';
  if(!primed){primed=true;$('#ss').value=st.ssid;$('#as').value=s.ap.ssid;$('#ao').checked=s.ap.open;$('#br').value=s.cfg.br;$('#dm').value=s.cfg.dim;$('#fl').checked=!!s.cfg.rot}
}
async function tick(){
  try{const r=await fetch('/api/status',{cache:'no-store'});if(!r.ok)throw 0;paint(await r.json())}
  catch(e){$('#sl').textContent='Tidak terhubung ke perangkat. Pastikan kamu tersambung ke hotspot repeater.'}
  setTimeout(tick,1000)
}
function renderNets(){
  $('#nl').innerHTML=nets.length?nets.map((n,i)=>'<li><button class="n" type="button" data-i="'+i+'"><span>'+esc(n.ssid)+'</span><span class="dim">'+(n.sec?'terkunci':'terbuka')+'</span><span>'+bars(n.rssi)+n.rssi+'</span></button></li>').join(''):'<li class="empty">Tidak ada jaringan ditemukan.</li>'
}
async function poll(){
  try{
    const j=await(await fetch('/api/scan',{cache:'no-store'})).json();
    if(j.scanning){setTimeout(poll,1200);return}
    nets=j.list;renderNets()
  }catch(e){setTimeout(poll,1500);return}
  $('#bs').disabled=false
}
$('#bs').onclick=async()=>{
  $('#bs').disabled=true;$('#nl').innerHTML='<li class="empty">Memindai...</li>';
  try{await fetch('/api/scan?start=1')}catch(e){}
  setTimeout(poll,1500)
};
$('#nl').onclick=e=>{
  const b=e.target.closest('button.n');if(!b)return;
  const n=nets[+b.dataset.i];$('#ss').value=n.ssid;$('#sp').value='';$('#sp').focus()
};
$('#sh').onchange=e=>{$('#sp').type=e.target.checked?'text':'password'};
$('#bc').onclick=()=>{
  const ssid=$('#ss').value.trim(),pass=$('#sp').value;
  if(!ssid)return toast('Isi nama WiFi dulu');
  if(pass&&pass.length<8)return toast('Sandi minimal 8 karakter');
  post('/api/sta',{ssid,pass})
};
$('#ab').onclick=()=>{
  const ssid=$('#as').value.trim(),pass=$('#ap').value,open=$('#ao').checked;
  if(!ssid)return toast('Isi nama hotspot dulu');
  if(!open&&pass&&pass.length<8)return toast('Sandi minimal 8 karakter');
  if(open&&!confirm('Hotspot tanpa sandi bisa dipakai siapa saja. Lanjutkan?'))return;
  post('/api/ap',{ssid,pass,open:open?'1':'0'});$('#ap').value=''
};
const disp=()=>post('/api/disp',{br:$('#br').value,dim:$('#dm').value,rot:$('#fl').checked?1:0});
$('#br').onchange=$('#dm').onchange=$('#fl').onchange=disp;
$('#rb').onclick=()=>{if(confirm('Mulai ulang repeater sekarang?'))post('/api/reboot',{})};
$('#fg').onclick=()=>{if(confirm('Hapus jaringan sumber yang tersimpan?'))post('/api/forget',{})};
tick();
</script>
</body>
</html>
)HTML";
