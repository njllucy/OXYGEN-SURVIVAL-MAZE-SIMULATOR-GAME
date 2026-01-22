#include <GL/glut.h>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <cmath>
#include <string>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <cstdio>
#ifdef _WIN32
  #include <windows.h>
#endif
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
// ---------- Basic types ----------
struct Vec2 { float x, y; };
float clampf(float v, float a, float b) { if (v < a) return a; if (v > b) return b; return v; }
// ---------- Window & game constants ----------
const int WIN_W = 800;
const int WIN_H = 600;
const float MAX_OXYGEN = 200.0f;
const float GAME_TIME = 20.0f; // countdown
// ---------- Entities ----------
struct Player {
    Vec2 pos;
    Vec2 vel;
    float radius;
    float oxygen;
    int collected;
} player;
struct Tank { Vec2 pos; float value; bool active; float radius; };
struct Hazard { Vec2 pos; Vec2 vel; float damage; float radius; bool active; };
struct ExitDoor { Vec2 pos; float radius; bool active; } exitDoor;
struct Blockade { float x, y, w, h; };
std::vector<Tank> tanks;
std::vector<Hazard> hazards;
std::vector<Blockade> blockades;
// ---------- Particles ----------
struct Particle { Vec2 pos; Vec2 vel; float life; };
std::vector<Particle> particles;
// ---------- Game state ----------
bool keys[256] = {0};
float lastTime = 0.0f;
bool paused = false;
bool gameOver = false;
bool playerWin = false;
int score = 0;

// spawn timers
float tankSpawnTimer = 0.0f;
float hazardSpawnTimer = 0.0f;
// countdown timer
float timeRemaining = GAME_TIME;
// beep helper for low-time alarm (accumulates dt)
float lowTimeBeepAccum = 0.0f;
// blink state for "HURRY!"
bool hurryVisible = true;
float hurryBlinkAccum = 0.0f;
// ---------- Door candidate positions ----------
std::vector<Vec2> doorPositions = {
    {50, WIN_H/2}, {WIN_W-50, WIN_H/2}, {WIN_W/2, 50}, {WIN_W/2, WIN_H-50}
};
// ---------- Utility ----------
float getTime() { return glutGet(GLUT_ELAPSED_TIME)/1000.0f; }
Vec2 randPos() { return { float(50 + rand() % (WIN_W-100)), float(50 + rand() % (WIN_H-100)) }; }
bool circleCollide(const Vec2 &a, float ra, const Vec2 &b, float rb){
    float dx = a.x-b.x, dy = a.y-b.y;
    return (dx*dx + dy*dy) <= (ra+rb)*(ra+rb);
}
bool isInsideBlockade(const Vec2& p, float radius) {
    for (auto& b : blockades) {
        if (p.x + radius > b.x &&
            p.x - radius < b.x + b.w &&
            p.y + radius > b.y &&
            p.y - radius < b.y + b.h) {
            return true;
        }
    }
    return false;
}
Vec2 getSafeRandomPos(float radius) {
    for (int i = 0; i < 200; i++) {   // try 200 times
        Vec2 p = randPos();
        if (!isInsideBlockade(p, radius)) {
            return p;
        }
    }
    return {WIN_W / 2.0f, WIN_H / 2.0f}; // fallback safe center
}
Vec2 getFarSafeSpawnFromDoor(float radius) {
    Vec2 bestPos = getSafeRandomPos(radius);
    float maxDist = 0;

    for (int i = 0; i < 200; i++) {
        Vec2 p = getSafeRandomPos(radius);
        float dx = p.x - exitDoor.pos.x;
        float dy = p.y - exitDoor.pos.y;
        float dist = sqrt(dx*dx + dy*dy);

        if (dist > maxDist) {
            maxDist = dist;
            bestPos = p;
        }
    }
    return bestPos;
}
//for water waves
void drawWaterBackground() {
    float t = getTime();

    glBegin(GL_LINES);
    for(int y = 0; y < WIN_H; y += 8) {
        float wave = sin(t*2 + y*0.05f) * 10;

        glColor3f(0.0f, 0.2f + y*0.0005f, 0.4f);
        glVertex2f(0, y + wave);
        glVertex2f(WIN_W, y + wave);
    }
    glEnd();
}
// ---------- Initialization ----------
void initBlockades(){
    blockades.clear();
    // sample maze-like path (kept as you had it)
    blockades.push_back({100, 0, 20, 400});
    blockades.push_back({300, 200, 20, 400});
    blockades.push_back({500, 0, 20, 400});
    blockades.push_back({700, 200, 20, 400});
    blockades.push_back({200, 500, 400, 20});
}
void initExitDoor(){
    int idx = rand() % doorPositions.size();
    //exitDoor.pos = doorPositions[idx];
    exitDoor.pos = getSafeRandomPos(exitDoor.radius + 10);
    exitDoor.radius = 20.0f;
    exitDoor.active = true;
}
void initGame(){
    initBlockades();
    initExitDoor();
    player.pos = getFarSafeSpawnFromDoor(player.radius + 5.0f);
    player.vel = {0,0};
    player.radius = 18.0f;
    player.oxygen = MAX_OXYGEN;
    player.collected = 0;
    tanks.clear(); hazards.clear(); particles.clear();
    tankSpawnTimer = 0.0f; hazardSpawnTimer = 0.0f; score = 0;
    gameOver = false; paused = false; playerWin = false;
    timeRemaining = GAME_TIME;
    lowTimeBeepAccum = 0.0f;
    hurryBlinkAccum = 0.0f;
    hurryVisible = true;
}
// ---------- Spawning ----------
void spawnTank(){
    Tank t;
    t.pos = randPos();
    t.value = 10;
    t.radius = 12;
    t.active = true;
    tanks.push_back(t);
}
void spawnHazard(){
    Hazard h; h.pos=randPos(); h.vel={float(rand()%201-100)/100.0f,float(rand()%201-100)/100.0f};
    h.damage=30; h.radius=14; h.active=true; hazards.push_back(h);
}
void spawnBubble(const Vec2 &p){
    Particle q; q.pos=p; q.vel={float(rand()%21-10)/50.0f, 0.5f + float(rand()%10)/100.0f}; q.life=1.0f+(rand()%100)/200.0f; particles.push_back(q);
}
// ---------- Update ----------
void update(float dt){
    if(paused || gameOver) return;
    // Safety: cap dt to avoid huge jumps (if app was paused by OS or debugging)
    const float MAX_DT = 0.25f; // allow up to 250 ms per frame
    if(dt > MAX_DT) dt = MAX_DT;
    // Countdown timer
    timeRemaining -= dt;
    if(timeRemaining < 0.0f) timeRemaining = 0.0f;
    if(timeRemaining <= 0.0f){
        // game over by timeout
        gameOver = true;
            paused = true;
    }
    // If low time (<10) handle blink and beep accumulation
    if(timeRemaining < 10.0f && timeRemaining > 0.0f){
        hurryBlinkAccum += dt;
        if(hurryBlinkAccum >= 0.4f){ // toggle every 0.4s
            hurryVisible = !hurryVisible;
            hurryBlinkAccum = 0.0f;
        }
        // beep once per second (accumulate dt)
        lowTimeBeepAccum += dt;
        if(lowTimeBeepAccum >= 1.0f){
            lowTimeBeepAccum -= 1.0f;
            #ifdef _WIN32
                Beep(800, 90); // short beep on Windows
            #endif
        }
    } else {
        // reset blink and beep accumulators when not low time
        hurryBlinkAccum = 0.0f;
        hurryVisible = true;
        lowTimeBeepAccum = 0.0f;
    }
    // Oxygen drain
    float baseDrain=5.0f, moveDrain=(fabs(player.vel.x)+fabs(player.vel.y))*0.02f;
    player.oxygen -= (baseDrain+moveDrain)*dt;
    player.oxygen = clampf(player.oxygen, 0.0f, MAX_OXYGEN);
    if(player.oxygen <= 0.01f) gameOver=true;
    // Input
    float accel=400.0f;
    Vec2 acc={0,0};
    if(keys['u']||keys['U']) acc.y += accel;
    if(keys['d']||keys['D']) acc.y -= accel;
    if(keys['l']||keys['L']) acc.x -= accel;
    if(keys['r']||keys['R']) acc.x += accel;
    player.vel.x += acc.x*dt;
    player.vel.y += acc.y*dt;
    player.vel.x *= 0.98f; player.vel.y *=0.98f;
    // Tentative move
    Vec2 nextPos = {player.pos.x + player.vel.x*dt, player.pos.y + player.vel.y*dt};
    // Blockade collision (same logic)
    bool blocked=false;
    for(auto &b:blockades){
        if(nextPos.x+player.radius > b.x && nextPos.x-player.radius < b.x+b.w &&
           nextPos.y+player.radius > b.y && nextPos.y-player.radius < b.y+b.h){
            blocked=true; break;
        }
    }
    if(!blocked){ player.pos=nextPos; }
    player.pos.x = clampf(player.pos.x,20.0f,WIN_W-20.0f);
    player.pos.y = clampf(player.pos.y,20.0f,WIN_H-20.0f);
    // spawn collectibles
    tankSpawnTimer+=dt; hazardSpawnTimer+=dt;
    if(tankSpawnTimer>4.0f){ spawnTank(); tankSpawnTimer=0.0f; }
    if(hazardSpawnTimer>3.0f){ spawnHazard(); hazardSpawnTimer=0.0f; }
    // hazards movement
    for(auto &h:hazards){
        if(!h.active) continue;
        h.pos.x += h.vel.x*60.0f*dt; h.pos.y += h.vel.y*60.0f*dt;
        if(h.pos.x<10||h.pos.x>WIN_W-10) h.vel.x=-h.vel.x;
        if(h.pos.y<10||h.pos.y>WIN_H-10) h.vel.y=-h.vel.y;
    }
    // particles
    for(auto &p:particles){ p.pos.x+=p.vel.x; p.pos.y+=p.vel.y; p.life-=dt; }
    particles.erase(std::remove_if(particles.begin(),particles.end(),[](const Particle &p){ return p.life<=0.0f; }),particles.end());
    // collision with tanks
    for(auto &t:tanks){
        if(!t.active) continue;
        if(circleCollide(player.pos,player.radius,t.pos,t.radius)){
            t.active=false; player.oxygen=clampf(player.oxygen+t.value,0.0f,MAX_OXYGEN);
            player.collected++; score+=10;
            for(int i=0;i<8;i++) spawnBubble(t.pos);
        }
    }
    // collision with hazards
    for(auto &h:hazards){
        if(!h.active) continue;
        if(circleCollide(player.pos,player.radius,h.pos,h.radius)){
            h.active=false; player.oxygen-=h.damage; score-=50;
            for(int i=0;i<18;i++){ Particle pp; pp.pos=h.pos; pp.vel={float(rand()%201-100)/60.0f,float(rand()%201-100)/60.0f}; pp.life=0.5f+(rand()%100)/200.0f; particles.push_back(pp); }
            if(player.oxygen<=0) gameOver=true;
        }
    }
    // collision with exit door (win)
    if(exitDoor.active && circleCollide(player.pos,player.radius,exitDoor.pos,exitDoor.radius)){
        gameOver=true; paused=true; playerWin=true; exitDoor.active=false;
        std::cout<<"YOU WIN! Congratulations!\n";
    }
    if((rand()%6)==0) spawnBubble({player.pos.x-player.radius-6.0f,player.pos.y});
}
// ---------- Rendering ----------
void drawCircle(float x,float y,float r){
    glBegin(GL_TRIANGLE_FAN); glVertex2f(x,y);
    int steps=24;
    for(int i=0;i<=steps;i++){ float a=(2.0f*M_PI*i)/steps; glVertex2f(x+cosf(a)*r,y+sinf(a)*r); }
    glEnd();
}
void drawText(float x,float y,const std::string &s){ glRasterPos2f(x,y); for(char c:s) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12,c); }

// Draw the instructions (bottom-left) — EXACT text you asked for
void drawInstructions() {
    glColor3f(1,1,1);
    float iy = 120.0f;
    drawText(3, iy,       "Press U - up");
    drawText(3, iy-20,    "Press D - down");
    drawText(3, iy-40,    "Press L - left");
    drawText(3, iy-60,    "Press R - right");
    drawText(3, iy-90,    "Touch green Ball To Win ");
}
// draw HURRY! if low time (blinking)
void drawHurryIfNeeded() {
    if(timeRemaining <= 10.0f && timeRemaining > 0.0f && hurryVisible) {
        glColor3f(1.0f, 0.2f, 0.2f);
        drawText(WIN_W/2 - 40, WIN_H - 100, "HURRY!");
    }
}
void display(){
    // KEEP YOUR WATER THEME
    glClearColor(0.02f,0.12f,0.22f,1.0f); glClear(GL_COLOR_BUFFER_BIT);
    glClear(GL_COLOR_BUFFER_BIT);
    drawWaterBackground();
    // blockades
    glColor3f(0.5f,0.3f,0.1f);
    for(auto &b:blockades){ glBegin(GL_QUADS); glVertex2f(b.x,b.y); glVertex2f(b.x+b.w,b.y); glVertex2f(b.x+b.w,b.y+b.h); glVertex2f(b.x,b.y+b.h); glEnd(); }
    // exit door
    if(exitDoor.active){ glColor3f(0,1,0); drawCircle(exitDoor.pos.x,exitDoor.pos.y,exitDoor.radius); }
    // tanks
    for(auto &t:tanks){ if(!t.active) continue; glColor3f(1.0f,0.9f,0.2f); drawCircle(t.pos.x,t.pos.y,t.radius); }
    // hazards
    for(auto &h:hazards){ if(!h.active) continue; glColor3f(0.8f,0.12f,0.12f); drawCircle(h.pos.x,h.pos.y,h.radius); }
// player submarine
glPushMatrix();
glTranslatef(player.pos.x, player.pos.y, 0);
float ang = atan2f(player.vel.y, player.vel.x) * 180.0f / M_PI;
glRotatef(ang, 0, 0, 1);
// body
glColor3f(0.2f, 0.6f, 0.8f);
glBegin(GL_POLYGON);
for(int i=0;i<360;i++){
    float a=i*M_PI/180.0f;
    glVertex2f(cos(a)*22, sin(a)*12);
}
glEnd();
// nose
glColor3f(0.8f, 0.8f, 0.9f);
glBegin(GL_TRIANGLES);
glVertex2f(22,0);
glVertex2f(35,6);
glVertex2f(35,-6);
glEnd();
// window
glColor3f(0.1f,0.2f,0.3f);
drawCircle(-5,0,5);
// tail
glColor3f(0.15f,0.4f,0.6f);
glBegin(GL_TRIANGLES);
glVertex2f(-22,0);
glVertex2f(-32,8);
glVertex2f(-32,-8);
glEnd();
glPopMatrix();
    // particles
    for(auto &p:particles){ float a=clampf(p.life,0.0f,1.0f); glColor4f(1,1,1,a); drawCircle(p.pos.x,p.pos.y,3.0f+(1.5f*(1.0f-a))); }
    // HUD
    glColor3f(1,1,1);
    std::stringstream ss; ss<<"Oxygen: "<<int(player.oxygen); drawText(12,WIN_H-22,ss.str());
    drawText(12,WIN_H-44,"Score: "+std::to_string(score));
    drawText(WIN_W-220,WIN_H-28,"Time Left: "+std::to_string(int(timeRemaining))+"s");
    float barW=200.0f; float pct=player.oxygen/MAX_OXYGEN;
    glBegin(GL_QUADS); glColor3f(0.2f,0.2f,0.2f); glVertex2f(12,WIN_H-60); glVertex2f(12+barW,WIN_H-60); glVertex2f(12+barW,WIN_H-72); glVertex2f(12,WIN_H-72); glEnd();
    glBegin(GL_QUADS); glColor3f(0.1f+0.6f*pct,0.1f+0.6f*(1.0f-pct),0.1f); glVertex2f(12,WIN_H-72); glVertex2f(12+barW*pct,WIN_H-72); glVertex2f(12+barW*pct,WIN_H-60); glVertex2f(12,WIN_H-60); glEnd();

    if(paused && !playerWin) drawText(WIN_W/2-40,WIN_H/3,"PAUSED");
    if(gameOver && !playerWin){ drawText(WIN_W/2-80,WIN_H/2+10,"GAME OVER - Press R to restart"); drawText(WIN_W/2-60,WIN_H/2-10,"Press ESC to exit"); }
    if(playerWin) drawText(WIN_W/2-60,WIN_H/4,"YOU WIN! Press R to restart");
    // draw instructions you requested (bottom-left)
    drawInstructions();
    // draw blinking HURRY if needed (time < 10)
    drawHurryIfNeeded();
    glutSwapBuffers();
}
// ---------- Input ----------
void keyDown(unsigned char k,int x,int y){
    keys[k]=true;
    if(k=='p'||k=='P') paused=!paused;
    if((k=='r'||k=='R') && (gameOver || playerWin)) initGame();
    if(k==27) std::exit(0);
}
void keyUp(unsigned char k,int x,int y){ keys[k]=false; }
// ---------- Timer ----------
void timerFunc(int v){
    float now=getTime();
    float dt=now-lastTime;
    // cap dt to avoid huge values (keeps timer stable)
    if(dt > 0.25f) dt = 0.25f;
    lastTime=now;
    update(dt);
    glutPostRedisplay();
    glutTimerFunc(16,timerFunc,0);
}
// ---------- Main ----------
int main(int argc,char**argv){
    srand((unsigned)time(nullptr));
    glutInit(&argc,argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
    glutInitWindowSize(WIN_W,WIN_H);
    glutCreateWindow("Oxygen Survival Maze Simulator - Countdown Timer");
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0,WIN_W,0,WIN_H,-1,1);
    glMatrixMode(GL_MODELVIEW);
    initGame();
    lastTime=getTime();
    glutDisplayFunc(display);
    glutKeyboardFunc(keyDown);
    glutKeyboardUpFunc(keyUp);
    glutTimerFunc(16,timerFunc,0);
    glutMainLoop();
    return 0;
}
