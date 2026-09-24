const socket = io()

function showPage(page){

document.querySelectorAll(".page").forEach(p=>p.style.display="none")

document.getElementById(page).style.display="block"

}

showPage("principal")

socket.on("vesc_data",(data)=>{

if(data.id==10){

document.getElementById("rpm1").innerText=data.rpm
document.getElementById("vin1").innerText=data.vin
document.getElementById("iin1").innerText=data.motor_current

}

if(data.id==11){

document.getElementById("rpm2").innerText=data.rpm
document.getElementById("vin2").innerText=data.vin
document.getElementById("iin2").innerText=data.motor_current

}

})

function updateClock(){

const now=new Date()

document.getElementById("clock").innerText=
now.toLocaleTimeString()

}

setInterval(updateClock,1000)