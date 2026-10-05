import { invoke, isShell } from "../lib/ipc";
import { el } from "../lib/format";
import { toast } from "../ui/shell";
import type { AuthSession, AuthStatus } from "../types";

const DISCORD_URL = "https://discord.gg/drxPk4htSd";
function openUrl(url:string){ invoke("sys.openUrl",{url}).catch(()=>window.open(url,"_blank")); }
function errText(e:unknown){ return (e instanceof Error?e.message:String(e)).replace(/^[A-Z_]+::/,""); }

export function renderAuth(root:HTMLElement,opts:{status:AuthStatus;onAuthed:(s:AuthSession)=>void}):void{
  root.innerHTML="";
  const wrap=el("div","auth-wrap");
  wrap.innerHTML=`<div class="auth-card glass" id="acard">
    <div class="auth-brand"><div class="mark"><img src="logo.png" alt="" /></div><h2>Log in</h2><p>ORBIT OPTIMIZER</p></div>
    <div class="auth-divider"><span>Continue</span></div>
    <div class="auth-err" id="aerr" role="alert"></div><div id="abody"></div>
    <div class="auth-status" id="astatus" aria-live="polite"></div>
    <button class="btn ghost block" id="acancel" type="button">Cancel</button>
    <div class="auth-foot"><span class="sdot pulse"></span><span>authentication ready</span><span style="flex:1"></span><a id="discord-link">Discord support</a></div>
  </div>`;
  root.appendChild(wrap);
  const body=wrap.querySelector("#abody") as HTMLElement, err=wrap.querySelector("#aerr") as HTMLElement, card=wrap.querySelector("#acard") as HTMLElement;
  const status=wrap.querySelector("#astatus") as HTMLElement;
  wrap.querySelector("#acancel")!.addEventListener("click",()=>{
    status.innerHTML='<span class="spin"></span> Cancelled — reload to try again';
  });
  let busy=false;
  const fail=(m:string)=>{err.textContent=m;err.classList.add("show");card.classList.remove("shake");void card.offsetWidth;card.classList.add("shake");};
  const clear=()=>err.classList.remove("show");
  const draw=()=>{body.innerHTML=`<button class="btn primary block sheen" id="discord"><span>Continue with Discord</span></button>
    <div style="height:12px"></div><button class="btn ghost block" id="keyauth">Activate KeyAuth License</button>
    <p class="sub" style="text-align:center;margin-top:14px">Discord login verifies your PRIMEx server membership and access role.</p>`;
    body.querySelector("#discord")!.addEventListener("click",onDiscord);
    body.querySelector("#keyauth")!.addEventListener("click",()=>openActivateInline());};
  async function onDiscord(){if(busy||!isShell())return;busy=true;clear();const b=body.querySelector("#discord") as HTMLButtonElement;b.disabled=true;b.innerHTML='<span class="spin"></span> Waiting for Discord…';status.innerHTML='<span class="spin"></span> Waiting for Discord…';try{const s=await invoke<AuthSession>("auth:discordLogin");status.textContent=`Welcome, ${s.username}`;toast({title:`Welcome, ${s.username}`,body:`${s.tierLabel} active.`,kind:"ok"});opts.onAuthed(s);}catch(e){fail(errText(e));status.textContent=""; }finally{busy=false;b.disabled=false;b.innerHTML="Continue with Discord";}}
  function openActivateInline(){clear();body.innerHTML=`<div class="ff"><input id="f-key" placeholder=" " autocomplete="off" spellcheck="false"/><label>Paste KeyAuth license key</label></div><button class="btn primary block sheen" id="go">Activate</button><button class="btn ghost block" id="back" style="margin-top:10px">Back</button>`;const input=body.querySelector("#f-key") as HTMLInputElement;input.focus();const run=async()=>{const k=input.value.trim();if(!k)return fail("Paste a license key first.");busy=true;const b=body.querySelector("#go") as HTMLButtonElement;b.disabled=true;b.innerHTML='<span class="spin"></span> Activating…';try{const s=await invoke<AuthSession>("auth:activateKey",{key:k});toast({title:"LIFETIME PREMIUM",body:"KeyAuth activation successful.",kind:"ok"});opts.onAuthed(s);}catch(e){fail(errText(e));}finally{busy=false;b.disabled=false;b.textContent="Activate";}};body.querySelector("#go")!.addEventListener("click",run);input.addEventListener("keydown",e=>{if((e as KeyboardEvent).key==="Enter")run();});body.querySelector("#back")!.addEventListener("click",draw);}
  wrap.querySelector("#discord-link")!.addEventListener("click",()=>openUrl(DISCORD_URL));
  draw();
}

export function openPremiumPrompt(onUpgraded?:()=>void):void{
  const ov=el("div","overlay");ov.innerHTML=`<div class="modal glass" role="dialog" aria-modal="true"><div class="prem-hero">PRIMEx Premium</div><h3 style="margin:8px 0 6px">This feature requires PRIMEx Premium.</h3><p class="sub">Upgrade through the PRIMEx Discord server or activate a KeyAuth lifetime license.</p><div class="row" style="display:flex;gap:10px;justify-content:flex-end"><button class="btn ghost" id="p-no">Maybe later</button><button class="btn primary" id="p-up">Get Premium</button></div></div>`;const done=()=>ov.remove();ov.querySelector("#p-no")!.addEventListener("click",done);ov.querySelector("#p-up")!.addEventListener("click",()=>{done();openUrl(DISCORD_URL);onUpgraded?.();});document.body.appendChild(ov);}

export function openActivateModal(onActivated:(s:AuthSession)=>void):void{
  const ov=el("div","modal-overlay");ov.innerHTML=`<div class="modal glass"><h3>Activate KeyAuth</h3><p class="sub">A successful KeyAuth activation grants lifetime premium.</p><div class="ff"><input id="ak" placeholder=" "/><label>License key</label></div><div class="modal-actions"><button class="btn ghost" id="cancel">Cancel</button><button class="btn primary" id="go">Activate</button></div></div>`;document.body.appendChild(ov);const key=ov.querySelector("#ak") as HTMLInputElement;key.focus();const done=()=>ov.remove();ov.querySelector("#cancel")!.addEventListener("click",done);ov.querySelector("#go")!.addEventListener("click",async()=>{try{const s=await invoke<AuthSession>("auth:activateKey",{key:key.value.trim()});done();onActivated(s);toast({title:"LIFETIME PREMIUM",body:"KeyAuth activation successful.",kind:"ok"});}catch(e){toast({title:"Activation failed",body:errText(e),kind:"err"});}});}

export function renderAuthLoading(root:HTMLElement):void{root.innerHTML="<div class='auth-wrap'><div class='auth-card glass' style='text-align:center'><div class='auth-brand'><div class='mark'><img src='logo.png' alt=''/></div><h2>ORBIT OPTIMIZER</h2></div><p class='sub'>Preparing authentication…</p></div></div>";}
