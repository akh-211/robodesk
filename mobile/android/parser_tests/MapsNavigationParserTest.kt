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
    println("PASS: Android Maps English/Indonesian instructions, unknown fallback, ETA, distance and UTF-8 bounds")
}
