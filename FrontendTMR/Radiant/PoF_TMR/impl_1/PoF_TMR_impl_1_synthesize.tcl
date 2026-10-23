if {[catch {

# define run engine funtion
source [file join {C:/lscc/radiant/2025.2} scripts tcl flow run_engine.tcl]
# define global variables
global para
set para(gui_mode) "1"
set para(prj_dir) "Z:/MyManjaro/HDisk/MyGithub/TinyCircularTMR/FrontendTMR/Radiant/PoF_TMR"
if {![file exists {Z:/MyManjaro/HDisk/MyGithub/TinyCircularTMR/FrontendTMR/Radiant/PoF_TMR/impl_1}]} {
  file mkdir {Z:/MyManjaro/HDisk/MyGithub/TinyCircularTMR/FrontendTMR/Radiant/PoF_TMR/impl_1}
}
cd {Z:/MyManjaro/HDisk/MyGithub/TinyCircularTMR/FrontendTMR/Radiant/PoF_TMR/impl_1}
# synthesize IPs
# synthesize VMs
# propgate constraints
file delete -force -- PoF_TMR_impl_1_cpe.ldc
::radiant::runengine::run_engine_newmsg cpe -syn lse -f "PoF_TMR_impl_1.cprj" "ZPLL.cprj" "ZFIFO.cprj" -a "iCE40UP"  -o PoF_TMR_impl_1_cpe.ldc
# synthesize top design
file delete -force -- PoF_TMR_impl_1.vm PoF_TMR_impl_1.ldc
::radiant::runengine::run_engine_newmsg synthesis -f "Z:/MyManjaro/HDisk/MyGithub/TinyCircularTMR/FrontendTMR/Radiant/PoF_TMR/impl_1/PoF_TMR_impl_1_lattice.synproj" -logfile "PoF_TMR_impl_1_lattice.srp"
::radiant::runengine::run_postsyn [list -a iCE40UP -p iCE40UP5K -t SG48 -sp High-Performance_1.2V -oc Industrial -top -w -o PoF_TMR_impl_1_syn.udb PoF_TMR_impl_1.vm] [list PoF_TMR_impl_1.ldc]

} out]} {
   ::radiant::runengine::runtime_log $out
   exit 1
}
