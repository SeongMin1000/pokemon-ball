// Gesture data collector — BNO055 6-axis (accel + gyro) time-series.
// On-device fixed-window recording at a precise sample rate, served as a
// Teachable-Machine-style web UI over STA (router) WiFi.
// Open http://xiao.local  (AP fallback: http://192.168.4.1)
//
// Each recording -> one CSV sample (Edge Impulse ready):
//   timestamp,accX,accY,accZ,gyrX,gyrY,gyrZ
// "전체 ZIP 다운로드" bundles them as  <label>.<n>.csv
//
// Based on bee2/edgeImpulse/gesture_collect — Adafruit getVector (WiFi-safe),
// 100 Hz, gyro in dps.  No BME280/BH1750 — BNO055 only.
//
// Wiring:  SDA=GPIO5(D4)  SCL=GPIO6(D5)  BNO055 @0x29  LED=GPIO21
#include <Wire.h>
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

#define SDA_PIN 5
#define SCL_PIN 6
#define LED_PIN 21

#define WIFI_SSID "projectbee"
#define WIFI_PASS "honeybear!"
#define MDNS_NAME "xiao"

#define MAX_SAMPLES 400        // 4 s @ 100 Hz headroom (400*6*4 = 9.6 KB RAM)

Adafruit_BNO055 bno = Adafruit_BNO055(55, 0x29, &Wire);
WebServer server(80);

static float bufA[MAX_SAMPLES][3];   // accelerometer m/s^2
static float bufG[MAX_SAMPLES][3];   // gyroscope   dps

static const char PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Pokemon Gesture Collector</title>
<style>
body{font-family:sans-serif;background:#111;color:#eee;text-align:center;margin:0;padding:16px}
h2{margin:6px}
.panel{background:#1c1c1c;border-radius:10px;padding:12px;max-width:680px;margin:10px auto}
.live{display:flex;gap:10px;justify-content:center;flex-wrap:wrap;font-family:monospace;font-size:14px}
.live div{background:#252525;border-radius:6px;padding:6px 10px;min-width:78px}
.live b{color:#9cf}
.cal span{display:inline-block;width:52px}
.bar{display:inline-block;height:8px;border-radius:4px;background:#4c8bf5;vertical-align:middle}
input,button{font-size:17px;padding:9px 14px;margin:5px;border-radius:8px;border:none}
input{width:120px;text-align:center}
input.sm{width:70px}
button{background:#4c8bf5;color:#fff;cursor:pointer}
button.rec{background:#f5734c}
button.dl{background:#3fbf6f}
button:disabled{opacity:.5;cursor:default}
.labels{display:flex;gap:6px;justify-content:center;flex-wrap:wrap;margin:6px 0}
.labels button{font-size:14px;padding:6px 14px;background:#333}
.labels button.active{background:#4c8bf5}
#status{font-size:14px;color:#9f9;margin:6px;min-height:18px}
#gallery{display:flex;flex-wrap:wrap;gap:8px;justify-content:center;max-width:680px;margin:12px auto}
.shot{position:relative;background:#222;border-radius:8px;padding:4px}
.shot canvas{display:block;border-radius:4px;background:#000}
.shot .lb{font-size:12px;color:#9cf;margin-top:2px}
.shot .x{position:absolute;top:-7px;right:-7px;width:22px;height:22px;line-height:20px;
  background:#e04c4c;color:#fff;border-radius:50%;cursor:pointer;font-weight:bold;font-size:14px}
</style></head><body>
<h2>Pokemon 제스처 수집기</h2>

<div class="panel">
  <div class="live">
    <div>ax <b id="ax">0</b></div><div>ay <b id="ay">0</b></div><div>az <b id="az">0</b></div>
    <div>gx <b id="gx">0</b></div><div>gy <b id="gy">0</b></div><div>gz <b id="gz">0</b></div>
  </div>
  <div class="cal" style="margin-top:8px;font-size:13px">
    calib &nbsp;
    <span>S<i id="cs" class="bar"></i></span>
    <span>G<i id="cg" class="bar"></i></span>
    <span>A<i id="ca" class="bar"></i></span>
    <span>M<i id="cm" class="bar"></i></span>
    <div style="color:#888;font-size:11px;margin-top:4px">수집 전에 보드를 천천히 돌려 G/A 막대를 채우세요 (3 = 완료)</div>
  </div>
</div>

<div class="panel">
  <div class="labels" id="labelBtns"></div>
  <input id="label" placeholder="라벨" value="idle">
  <label style="font-size:13px">길이<input id="ms" class="sm" type="number" value="2000">ms</label>
  <label style="font-size:13px">주파수<input id="hz" class="sm" type="number" value="100">Hz</label>
  <br>
  <button id="recBtn" class="rec" onclick="rec()">● 녹화</button>
  <button class="dl" onclick="zipAll()">전체 ZIP 다운로드</button>
  <button style="background:#666" onclick="clearAll()">전체 비우기</button>
  <div id="status">라벨을 정하고 '녹화'를 누른 뒤 제스처를 취하세요.</div>
</div>

<div id="gallery"></div>

<script>
// ---- quick-select label buttons ----
const LABELS=['idle','left','right','up','down','circle'];
const lb=document.getElementById('labelBtns');
LABELS.forEach(l=>{const b=document.createElement('button');b.textContent=l;
  b.onclick=()=>{document.getElementById('label').value=l;
    lb.querySelectorAll('button').forEach(x=>x.classList.remove('active'));b.classList.add('active');};
  lb.appendChild(b);});

let shots=[];   // {label, csv, rows:[[t,ax,ay,az,gx,gy,gz],...]}

// ---- live readout (5 Hz) ----
setInterval(async()=>{
  try{
    const r=await fetch('/live?t='+Date.now());
    if(!r.ok)return; const d=await r.json();
    ax.textContent=d.ax.toFixed(2); ay.textContent=d.ay.toFixed(2); az.textContent=d.az.toFixed(2);
    gx.textContent=d.gx.toFixed(1); gy.textContent=d.gy.toFixed(1); gz.textContent=d.gz.toFixed(1);
    setBar('cs',d.cs); setBar('cg',d.cg); setBar('ca',d.ca); setBar('cm',d.cm);
  }catch(e){}
},200);
function setBar(id,v){ const e=document.getElementById(id);
  e.style.width=(4+v*10)+'px'; e.style.background=v>=3?'#3fbf6f':(v>=1?'#f5c14c':'#e04c4c'); }

// ---- record one fixed window on the device ----
async function rec(){
  const label=document.getElementById('label').value.trim()||'unlabeled';
  const ms=Math.max(200,Math.min(4000,+document.getElementById('ms').value||2000));
  const hz=Math.max(10,Math.min(200,+document.getElementById('hz').value||100));
  const btn=document.getElementById('recBtn'); btn.disabled=true;
  status.textContent='● 녹화 중... 제스처를 취하세요';
  try{
    const r=await fetch('/record?ms='+ms+'&hz='+hz+'&t='+Date.now());
    const csv=await r.text();
    const rows=csv.trim().split('\n').slice(1).map(l=>l.split(',').map(Number));
    shots.push({label,csv,rows}); render();
    status.textContent=label+' 샘플 저장됨 ('+rows.length+' 포인트) · 총 '+shots.length+'개';
  }catch(e){ status.textContent='녹화 실패: '+e; }
  btn.disabled=false;
}

function del(i){ shots.splice(i,1); render(); }
function clearAll(){ if(!shots.length||!confirm('전부 삭제할까요?'))return; shots=[]; render();
  status.textContent='비웠습니다.'; }

function render(){
  const g=document.getElementById('gallery'); g.innerHTML='';
  shots.forEach((s,i)=>{
    const d=document.createElement('div'); d.className='shot';
    d.innerHTML='<div class="x" onclick="del('+i+')">x</div><div class="lb">'+s.label+' #'+(i+1)+'</div>';
    const cv=document.createElement('canvas'); cv.width=100; cv.height=48; d.insertBefore(cv,d.firstChild.nextSibling);
    plot(cv,s.rows); g.appendChild(d);
  });
}
// tiny 6-axis sparkline
function plot(cv,rows){
  const c=cv.getContext('2d'), W=cv.width,H=cv.height;
  const cols=['#ff6b6b','#6bff8f','#6b9bff','#ffd36b','#d36bff','#6bffea'];
  let mn=1e9,mx=-1e9;
  for(const r of rows)for(let k=1;k<7;k++){mn=Math.min(mn,r[k]);mx=Math.max(mx,r[k]);}
  const sp=mx-mn||1;
  for(let k=1;k<7;k++){ c.strokeStyle=cols[k-1]; c.beginPath();
    rows.forEach((r,j)=>{ const x=j/(rows.length-1)*W, y=H-(r[k]-mn)/sp*H;
      j?c.lineTo(x,y):c.moveTo(x,y); }); c.stroke(); }
}

// ---- minimal ZIP (store, no compression) ----
const CRC_T=(()=>{let t=[];for(let n=0;n<256;n++){let c=n;for(let k=0;k<8;k++)c=c&1?0xEDB88320^(c>>>1):c>>>1;t[n]=c>>>0}return t})();
function crc32(u8){let c=0xFFFFFFFF;for(let i=0;i<u8.length;i++)c=CRC_T[(c^u8[i])&0xFF]^(c>>>8);return (c^0xFFFFFFFF)>>>0}
function zipAll(){
  if(!shots.length){alert('보관된 샘플이 없습니다');return}
  const enc=new TextEncoder(), parts=[], cdir=[]; let off=0, counts={};
  for(const s of shots){
    counts[s.label]=(counts[s.label]||0)+1;
    const name=enc.encode(s.label+'.'+counts[s.label]+'.csv');
    const data=enc.encode(s.csv);
    const crc=crc32(data), sz=data.length;
    const lh=new DataView(new ArrayBuffer(30));
    lh.setUint32(0,0x04034b50,true);lh.setUint16(4,20,true);lh.setUint32(14,crc,true);
    lh.setUint32(18,sz,true);lh.setUint32(22,sz,true);lh.setUint16(26,name.length,true);
    parts.push(new Uint8Array(lh.buffer),name,data);
    const cd=new DataView(new ArrayBuffer(46));
    cd.setUint32(0,0x02014b50,true);cd.setUint16(4,20,true);cd.setUint16(6,20,true);
    cd.setUint32(16,crc,true);cd.setUint32(20,sz,true);cd.setUint32(24,sz,true);
    cd.setUint16(28,name.length,true);cd.setUint32(42,off,true);
    cdir.push(new Uint8Array(cd.buffer),name);
    off+=30+name.length+sz;
  }
  let cdLen=0; cdir.forEach(p=>cdLen+=p.length);
  const end=new DataView(new ArrayBuffer(22));
  end.setUint32(0,0x06054b50,true);end.setUint16(8,shots.length,true);end.setUint16(10,shots.length,true);
  end.setUint32(12,cdLen,true);end.setUint32(16,off,true);
  const blob=new Blob([...parts,...cdir,new Uint8Array(end.buffer)],{type:'application/zip'});
  const a=document.createElement('a');
  a.href=URL.createObjectURL(blob); a.download='gesture_dataset.zip'; a.click();
  URL.revokeObjectURL(a.href);
}
</script></body></html>
)HTML";

void handleRoot() { server.send_P(200, "text/html", PAGE); }

void handleLive() {
  imu::Vector<3> a = bno.getVector(Adafruit_BNO055::VECTOR_ACCELEROMETER);
  imu::Vector<3> g = bno.getVector(Adafruit_BNO055::VECTOR_GYROSCOPE);
  uint8_t cs, cg, ca, cm; bno.getCalibration(&cs, &cg, &ca, &cm);
  char json[192];
  snprintf(json, sizeof(json),
    "{\"ax\":%.2f,\"ay\":%.2f,\"az\":%.2f,\"gx\":%.1f,\"gy\":%.1f,\"gz\":%.1f,"
    "\"cs\":%u,\"cg\":%u,\"ca\":%u,\"cm\":%u}",
    a.x(), a.y(), a.z(), g.x(), g.y(), g.z(), cs, cg, ca, cm);
  server.send(200, "application/json", json);
}

// Precise on-device fixed-window capture, streamed back as CSV.
void handleRecord() {
  int hz = server.hasArg("hz") ? server.arg("hz").toInt() : 100;
  int ms = server.hasArg("ms") ? server.arg("ms").toInt() : 2000;
  if (hz < 10)  hz = 10;   if (hz > 200)  hz = 200;
  if (ms < 200) ms = 200;  if (ms > 4000) ms = 4000;
  int n = (long)ms * hz / 1000;
  if (n > MAX_SAMPLES) n = MAX_SAMPLES;
  uint32_t interval = 1000000UL / hz;   // microseconds per sample

  digitalWrite(LED_PIN, LOW);           // LED on during capture
  uint32_t next = micros();
  for (int i = 0; i < n; i++) {
    while ((int32_t)(micros() - next) < 0) { }   // busy-wait to sample instant
    imu::Vector<3> a = bno.getVector(Adafruit_BNO055::VECTOR_ACCELEROMETER);
    imu::Vector<3> g = bno.getVector(Adafruit_BNO055::VECTOR_GYROSCOPE);
    bufA[i][0] = a.x(); bufA[i][1] = a.y(); bufA[i][2] = a.z();
    bufG[i][0] = g.x(); bufG[i][1] = g.y(); bufG[i][2] = g.z();
    next += interval;
  }
  digitalWrite(LED_PIN, HIGH);

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv", "");
  server.sendContent("timestamp,accX,accY,accZ,gyrX,gyrY,gyrZ\n");
  char line[96];
  for (int i = 0; i < n; i++) {
    int t = (int)((long)i * 1000 / hz);   // ms
    snprintf(line, sizeof(line), "%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n",
             t, bufA[i][0], bufA[i][1], bufA[i][2],
             bufG[i][0], bufG[i][1], bufG[i][2]);
    server.sendContent(line);
  }
  server.sendContent("");   // end chunked response
}

void setup() {
  Serial.begin(115200);
  delay(500);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);
  if (!bno.begin()) {
    Serial.println("[BNO] NOT FOUND! check wiring (addr 0x29)");
    while (true) { digitalWrite(LED_PIN, LOW); delay(200); digitalWrite(LED_PIN, HIGH); delay(200); }
  }
  bno.setExtCrystalUse(true);
  Serial.println("[BNO] OK");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) delay(200);
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("STA connected. Open http://");
    Serial.println(WiFi.localIP());
    if (MDNS.begin(MDNS_NAME)) Serial.println("mDNS: http://" MDNS_NAME ".local");
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("XIAO_GESTURE", "12345678");
    Serial.print("Router unreachable - AP fallback. Open http://");
    Serial.println(WiFi.softAPIP());
  }

  server.on("/", handleRoot);
  server.on("/live", handleLive);
  server.on("/record", handleRecord);
  server.begin();
  Serial.println("Gesture collector ready");
}

void loop() { server.handleClient(); }
