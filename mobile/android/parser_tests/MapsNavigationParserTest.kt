package com.robodesk.phonebridge
fun main(){
    val left=MapsNavigationParser.parse("Belok kiri","200 m menuju Jalan Merdeka","5 menit",false)!!
    check(left.turn=="left"&&left.distanceMeters==200&&left.eta=="5 menit")
    val right=MapsNavigationParser.parse("Turn right","1.5 km onto Road A","12 min",false)!!
    check(right.turn=="right"&&right.distanceMeters==1500)
    check(MapsNavigationParser.parse("Keep left","0,2 km","",false)!!.turn=="slight_left")
    check(MapsNavigationParser.parse("Putar balik","50 m","",false)!!.turn=="uturn")
    check(MapsNavigationParser.parse("Menghitung ulang","","",false)!!.state=="rerouting")
    check(MapsNavigationParser.parse("Tiba di tujuan","","",false)!!.state=="arrived")
    check(MapsNavigationParser.parse("Unrecognized navigation","","",true)!!.wire().toString(Charsets.UTF_8).contains("\nunknown\n?\n"))
    check(MapsNavigationParser.parse("Restaurant nearby","200 m","",false)==null)
    check(utf8Field("🚗🚗🚗",5)=="🚗")
    check(!NavigationInstruction("active","left",1,"Road\nName","5 min").wire().toString(Charsets.UTF_8).contains("Road\nName"))
    // PD-5.4: imperial units, exits/forks, merge, road name when the title is only a distance, clock ETA.
    check(MapsNavigationParser.parse("Turn left","500 ft onto Main St","",false)!!.distanceMeters==152)
    check(MapsNavigationParser.parse("Continue","0.5 mi","",true)!!.distanceMeters==805)
    check(MapsNavigationParser.parse("Take the exit on the right","300 m","",false)!!.turn=="slight_right")
    check(MapsNavigationParser.parse("Ambil jalan keluar kiri","1 km","",false)!!.turn=="slight_left")
    check(MapsNavigationParser.parse("Merge onto Toll Road","200 m","",false)!!.turn=="straight")
    check(MapsNavigationParser.parse("200 m","Turn left onto Jalan Merdeka","12 min · 5 km · 10:42",false)!!.let { it.road=="Turn left onto Jalan Merdeka" && it.eta=="12 min 10:42" && it.turn=="left" })
    check(MapsNavigationParser.parse("Turn right","20 meters","",false)!!.distanceMeters==20)
    println("PASS: Android Maps English/Indonesian instructions, unknown fallback, ETA, distance and UTF-8 bounds")
}
