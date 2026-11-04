/* Browser-controller checks against the actual embedded page. No device/network. */
const assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path'),vm=require('node:vm');
const header=fs.readFileSync(path.join(__dirname,'../include/portal_page.h'),'utf8');
const html=header.match(/R"PORTAL\(([\s\S]*)\)PORTAL"/)[1],script=html.match(/<script>([\s\S]*)<\/script>/)[1];
class Element {
 constructor(){this.value='';this.textContent='';this.children=[];this.hidden=false;this.disabled=false;this.checked=false;this.attrs={};this.dataset={};this.classList={toggle:()=>{}};}
 setAttribute(k,v){this.attrs[k]=v;}
 append(...children){this.children.push(...children);}
 replaceChildren(...children){this.children=children;}
 click(){return this.onclick?.();}
}
const ids=[...html.matchAll(/\bid="([^"]+)"/g)].map(match=>match[1]);
const nodes=Object.fromEntries(ids.map(id=>[id,new Element()]));
const tabs=['ns','wifi','display'].map(tab=>{const node=new Element();node.dataset.tab=tab;return node;});
let authenticated=false,failSave=false;const requests=[];
let config={revision:1,csrf:'fixture-csrf',url:'https://example.com',tokenSet:true,device:'',timezone:'UTC0',ntp1:'pool.ntp.org',ntp2:'time.cloudflare.com',hours:3,poll:60,sgvStale:600,apsStale:600,low:70,high:180,brightness:100,volume:80,v3:true,mmol:false,forecast:true,sound:true,connectedSsid:'Backup',wifi:[{ssid:'Primary',passwordSet:true},{ssid:'Backup',passwordSet:true}]};
const response=(status,data)=>({status,ok:status<400,json:async()=>JSON.parse(JSON.stringify(data))});
async function fakeFetch(url,options){
 const body=options.body?JSON.parse(options.body):undefined;requests.push({url,options,body});
 if(url==='/login'){authenticated=body.code==='12345678';return response(authenticated?200:401,authenticated?{message:'Unlocked'}:{error:'Incorrect access code'});}
 if(!authenticated)return response(401,{error:'Authentication required'});
 if(options.method==='POST')assert.equal(options.headers['X-CSRF-Token'],'fixture-csrf');
 if(url==='/api/settings'&&options.method==='GET')return response(200,config);
 if(url==='/api/settings'&&options.method==='POST'){
  if(failSave)return response(409,{error:'Settings changed; reload before saving'});
  const previous=config;config={...config,...body,revision:config.revision+1,tokenSet:true,wifi:body.wifi.map(n=>({ssid:n.ssid,passwordSet:!n.open&&(!!n.password||previous.wifi.find(old=>old.ssid===n.ssid)?.passwordSet)}))};delete config.token;return response(200,{message:'Settings saved.'});
 }
 if(url==='/api/test')return response(200,{message:'HTTPS/auth OK; entries readable.'});
 if(url==='/api/reconnect')return response(200,{message:'Reconnect requested.'});
 if(url==='/logout'){authenticated=false;return response(200,{message:'Locked.'});}
 throw Error('Unexpected request '+url);
}
const sandbox={document:{getElementById:id=>{assert(nodes[id],id);return nodes[id];},createElement:()=>new Element(),createTextNode:text=>({textContent:text}),querySelectorAll:()=>tabs},location:{origin:'http://192.168.1.42:3000'},fetch:fakeFetch,URL,console};
vm.runInNewContext(script,sandbox);
const settle=()=>new Promise(resolve=>setImmediate(resolve));
(async()=>{
 await settle();assert.equal(nodes.login.hidden,false);assert.equal(nodes.app.hidden,true);
 nodes.code.value='bad';await nodes.unlock.click();assert.equal(nodes.app.hidden,true);assert.equal(nodes.message.textContent,'Incorrect access code');
 nodes.code.value='12345678';await nodes.unlock.click();assert.equal(nodes.app.hidden,false);assert.equal(nodes.token.value,'');assert.equal(nodes.code.value,'');
 tabs[1].click();assert.equal(nodes.wifi.hidden,false);assert.equal(nodes.ns.hidden,true);
 nodes.networks.children[1].children[0].children[1].children[0].click();
 await nodes.save.click();let sent=requests.filter(r=>r.url==='/api/settings'&&r.options.method==='POST').at(-1);
 assert.equal(sent.body.token,'');assert.equal(sent.body.wifi[0].ssid,'Backup');assert.equal(sent.body.wifi[0].password,'');
 nodes.token.value='replacement-fixture-token';await nodes.save.click();assert.equal(nodes.token.value,'');
 nodes.add.click();assert.equal(nodes.networks.children.length,3);
 const added=nodes.networks.children[2],name=added.children[1].children[0].children[0],password=added.children[1].children[1].children[0];
 name.value='Travel';name.oninput();password.value='fixture-new-password';password.oninput();
 await nodes.save.click();sent=requests.filter(r=>r.url==='/api/settings'&&r.options.method==='POST').at(-1);assert.equal(sent.body.wifi[2].ssid,'Travel');assert.equal(sent.body.wifi[2].password,'fixture-new-password');
 await nodes.test.click();sent=requests.filter(r=>r.url==='/api/test').at(-1);assert.equal(sent.body.wifi,undefined);assert.equal(sent.body.url,'https://example.com');
 failSave=true;await nodes.save.click();assert.equal(nodes.message.textContent,'Settings changed; reload before saving');assert.equal(nodes.save.disabled,false);
 await nodes.logout.click();assert.equal(nodes.app.hidden,true);assert.equal(nodes.token.value,'');assert.equal(nodes.networks.children.length,0);
 assert(requests.every(r=>r.options.credentials==='same-origin'&&r.options.cache==='no-store'));
 console.log('Portal frontend checks passed: login, secret preservation, priority editing, new network payload, CSRF forwarding, stale-save errors and lock cleanup.');
})().catch(error=>{console.error(error);process.exitCode=1;});
