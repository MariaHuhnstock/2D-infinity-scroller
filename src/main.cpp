#include "ofMain.h"
#include <fstream>

// Sammlung der verfügbaren Hintergrund-Tiles (z. B. 4 Stück)
std::vector<ofImage> tilePool;
// Queue der aktuell sichtbaren Tiles im Hintergrund
std::deque<ofImage> tilesToDraw;
// Index zur korrekten vertikalen Positionierung beim Scrollen
int tileDrawOffsetIndex = 0;

// Struktur für Schüsse (Position und Geschwindigkeit)
struct Shot
{
    glm::vec2 pos;
    glm::vec2 vel;
};

// Struktur für Hindernisse, die Up-/Downgrades geben
struct Obstacle
{
    enum Type { UPGRADE_FIRE, DOWNGRADE_FIRE, UPGRADE_MULTI, DOWNGRADE_MULTI } type;
    glm::vec2 pos;
    float width = 360;
    float height = 40;
};

// Gegner mit Typ, Position, Größe (Radius) und Lebenspunkten
struct Enemy
{
    enum Type { YELLOW, ORANGE, RED } type;
    glm::vec2 pos;
    float radius;
    int hp;
};

// Boss-Gegner mit größerem Radius, mehr HP etc.
struct Boss
{
    glm::vec2 pos;
    int hp;
    float radius;
    bool active = false;
};

class Player
{
public:
    glm::vec2 pos;

    // Permanente (gekaufte) Basiswerte – gelten zu Beginn jedes Levels
    float baseFireRate = 0.5f;     // Basis-Schussfrequenz (niedriger = schneller)
    int baseMultiShot = 1;         // Basis-Multishot (Anzahl der Schüsse)
    int baseMaxHP = 5;             // Basis-Maximale Lebenspunkte

    // Aktuelle (temporär veränderte) Werte – können durch Hindernisse modifiziert werden
    float fireRate = baseFireRate;
    int multiShot = baseMultiShot;
    int hitpoints = baseMaxHP;     // Aktuelle Lebenspunkte

    void setPosition(glm::vec2 p) { pos = p; }

    void draw()
    {
        ofSetColor(255);
        float size = 40;
        ofDrawTriangle(pos.x, pos.y - size, pos.x - size * 0.75f, pos.y + size, pos.x + size * 0.75f, pos.y + size);
    }

    // Upgrade durch Hindernis
    void applyUpgrade(Obstacle::Type type)
    {
        if (type == Obstacle::UPGRADE_FIRE)
            fireRate = std::max(0.1f, fireRate - 0.05f);
        else if (type == Obstacle::UPGRADE_MULTI)
            multiShot = std::min(7, multiShot + 1);
    }

    // Downgrade durch Hindernis
    void applyDowngrade(Obstacle::Type type)
    {
        if (type == Obstacle::DOWNGRADE_FIRE)
            fireRate = std::min(1.0f, fireRate + 0.1f);
        else if (type == Obstacle::DOWNGRADE_MULTI)
            multiShot = std::max(1, multiShot - 1);
    }
};

class ofApp : public ofBaseApp
{
public:
    // Grundfunktionen
    void setup();
    void update();
    void draw();
    void keyPressed(int key);
    void restartGame();

    // Spielobjekte
    Player player;
    std::vector<Shot> playerShots, enemyShots;
    std::vector<Obstacle> obstacles;
    std::vector<Enemy> enemies;
    std::vector<ofImage> tiles;

    // Zeitsteuerung
    float shootTimer = 0, obstacleSpawnTimer = 0, enemySpawnTimer = 0, enemyShootTimer = 0;
    float backgroundOffset = 0;
    const float scrollSpeed = 100;

    // Boss-System
    Boss currentBoss;
    bool bossDefeated = false, bossSpawned = false;
    int enemiesToDefeatBeforeBoss = 15;

    // Game-State
    bool gameOver = false, win = false;
    bool inUpgradeMenu = false;

    // Fortschritt
    int currentLevel = 1;
    int maxHP = 5;
    int enemiesDefeated = 0;
    int score = 0;
    int highscore = 0;
    bool newHighscore = false;
};

void ofApp::setup()
{
    // Grundeinstellungen für das Spiel
    ofSetVerticalSync(true);
    ofSetFrameRate(60);
    ofBackground(0);
    ofSetWindowShape(730, 1280);

    // Spielerposition initialisieren
    player.setPosition(glm::vec2(ofGetWidth() / 2, ofGetHeight() - 50));
    player.hitpoints = maxHP;

    // Anfangswerte für Spieler setzen
    player.baseFireRate = 0.5f;
    player.baseMultiShot = 1;
    player.baseMaxHP = 5;

    player.fireRate = player.baseFireRate;
    player.multiShot = player.baseMultiShot;
    maxHP = player.baseMaxHP;
    player.hitpoints = maxHP;

    // Hintergrundgrafiken laden
    for (int i = 0; i < 4; ++i) {
        ofImage img;
        if (img.load("background" + std::to_string(i) + ".png"))
        {
            tilePool.push_back(img);
        }
        else
        {
            ofLogError() << "Fehler beim Laden von background" << i << ".png";
        }
    }

    // Start-Tiles zufällig auswählen
    for (int i = 0; i < 5; ++i)
    {
        int r = ofRandom(tilePool.size());
        tilesToDraw.push_back(tilePool[r]);
    }

    // Highscore aus Datei laden
    std::ifstream file("highscore.txt");
    if (file.is_open())
    {
        file >> highscore;
        file.close();
    }
}

void ofApp::keyPressed(int key)
{
    // Spiel neustarten mit Taste 'R'
    if (key == 'r' || key == 'R')
    {
        // Nur erlauben, wenn Game Over ist
        if (gameOver)
        {
            restartGame();
        }
    }

    if (!inUpgradeMenu) return;

    // Taste 1: Dauerhaft schnellere Feuerrate kaufen
    if (key == '1' && score >= 1000)
    {
        player.baseFireRate = std::max(0.1f, player.baseFireRate - 0.05f);
        score -= 1000;
    }
    // Taste 2: Dauerhaft mehrfache Schüsse kaufen
    else if (key == '2' && score >= 2000)
    {
        player.baseMultiShot = std::min(7, player.baseMultiShot + 1);
        score -= 2000;
    }
    // Taste 3: Dauerhaft maximale HP erhöhen (setzt HP vollständig zurück)
    else if (key == '3' && score >= 3000)
    {
        player.baseMaxHP++;
        score -= 3000;
    }
    // ENTER: Neues Level starten – temporäre Werte zurücksetzen
    else if (key == OF_KEY_RETURN)
    {
        currentLevel++;
        enemiesDefeated = 0;
        bossSpawned = false;
        inUpgradeMenu = false;

        // Spieler auf Basiswerte zurücksetzen
        player.fireRate = player.baseFireRate;
        player.multiShot = player.baseMultiShot;
        maxHP = player.baseMaxHP;
        player.hitpoints = maxHP;
    }
}


void ofApp::update()
{
    // Spiel pausiert, wenn es vorbei ist, gewonnen wurde oder man im Upgrade-Menü ist
    if (gameOver || win || inUpgradeMenu) return;

    // Zeit seit dem letzten Frame (für framerate-unabhängige Bewegungen)
    float dt = ofGetLastFrameTime();

    // Spielerposition aktualisieren: horizontal folgt er der Maus
    player.setPosition(glm::vec2(ofGetMouseX(), player.pos.y));

    // Hintergrundscrolling nach unten
    backgroundOffset += scrollSpeed * dt;

    // Hindernisse & Schüsse nach unten oder oben bewegen
    for (auto& obs : obstacles) obs.pos.y += 100 * dt;
    for (auto& s : enemyShots) s.pos += s.vel * dt;
    for (auto& s : playerShots) s.pos += s.vel * dt;

    // Spieler feuert automatisch bei passendem Intervall
    shootTimer += dt;
    if (shootTimer >= player.fireRate)
    {
        for (int i = 0; i < player.multiShot; ++i)
        {
            float offset = (i - (player.multiShot - 1) / 2.0f) * 15.0f;
            playerShots.push_back({ player.pos + glm::vec2(offset, 0), glm::vec2(0, -600) });
        }
        shootTimer = 0;
    }

    // Gegner feuern in Intervallen
    enemyShootTimer += dt;
    if (enemyShootTimer >= 1.5f)
    {
        for (auto& e : enemies)
            enemyShots.push_back({ e.pos, glm::vec2(0, 300) });
        enemyShootTimer = 0;
    }

    // Gegner bewegen sich nach unten – bei Bildschirmende Schaden & entfernen
    for (auto eIt = enemies.begin(); eIt != enemies.end(); )
    {
        eIt->pos.y += 100 * dt;
        if (eIt->pos.y > ofGetHeight())
        {
            player.hitpoints = std::max(0, player.hitpoints - 2);
            eIt = enemies.erase(eIt);
        }
        else ++eIt;
    }

    // Kollisionsabfrage: Spielerschüsse treffen Gegner
    for (auto sIt = playerShots.begin(); sIt != playerShots.end(); )
    {
        bool hit = false;
        for (auto eIt = enemies.begin(); eIt != enemies.end(); ++eIt)
        {
            if (glm::distance(sIt->pos, eIt->pos) < eIt->radius)
            {
                eIt->hp--;
                if (eIt->hp <= 0)
                {
                    score += 100 * (eIt->radius / 20); // Punkte abhängig von Größe
                    eIt = enemies.erase(eIt);
                    enemiesDefeated++;
                }
                hit = true;
                break;
            }
        }
        if (hit) sIt = playerShots.erase(sIt);
        else ++sIt;
    }

    // Kollisionsabfrage: Gegnerschüsse treffen Spieler
    for (auto sIt = enemyShots.begin(); sIt != enemyShots.end(); )
    {
        if (glm::distance(sIt->pos, player.pos) < 15)
        {
            player.hitpoints = std::max(0, player.hitpoints - 1);
            sIt = enemyShots.erase(sIt);
        }
        else ++sIt;
    }

    // Schüsse außerhalb des Bildschirms entfernen
    playerShots.erase(remove_if(playerShots.begin(), playerShots.end(), [](Shot& s) { return s.pos.y < 0; }), playerShots.end());
    enemyShots.erase(remove_if(enemyShots.begin(), enemyShots.end(), [](Shot& s) { return s.pos.y > ofGetHeight(); }), enemyShots.end());

    // Hindernis-Kollision mit Spieler prüfen
    for (auto it = obstacles.begin(); it != obstacles.end(); )
    {
        bool colX = abs(it->pos.x - player.pos.x) < it->width / 2;
        bool colY = abs(it->pos.y - player.pos.y) < it->height / 2 + 15;
        if (colX && colY)
        {
            if (it->type == Obstacle::UPGRADE_FIRE || it->type == Obstacle::UPGRADE_MULTI)
                player.applyUpgrade(it->type);
            else
                player.applyDowngrade(it->type);
            score += 50;
            it = obstacles.erase(it);
        }
        else ++it;
    }

    // Hindernisse spawnen in Intervallen mit Zufallstyp
    obstacleSpawnTimer += dt;
    if (obstacleSpawnTimer >= 2.5f)
    {
        Obstacle obs;
        float halfScreen = ofGetWidth() / 2;
        bool left = (ofRandom(1.0f) < 0.5f);
        obs.pos = glm::vec2(left ? halfScreen / 2 : halfScreen + halfScreen / 2, -100);
        float r = ofRandom(1.0f);
        obs.type = r < 0.25f ? Obstacle::UPGRADE_FIRE :
            r < 0.5f ? Obstacle::DOWNGRADE_FIRE :
            r < 0.75f ? Obstacle::UPGRADE_MULTI :
            Obstacle::DOWNGRADE_MULTI;
        obstacles.push_back(obs);
        obstacleSpawnTimer = 0;
    }

    // Gegner spawnen regelmäßig, Typ hängt vom Zufall ab
    enemySpawnTimer += dt;
    if (enemySpawnTimer >= 1.5f)
    {
        Enemy e;
        float r = ofRandom(1.0f);
        if (r < 0.6f) { e.type = Enemy::YELLOW; e.hp = 1; e.radius = 20; }
        else if (r < 0.9f) { e.type = Enemy::ORANGE; e.hp = 2; e.radius = 25; }
        else { e.type = Enemy::RED;    e.hp = 3; e.radius = 30; }
        e.pos = glm::vec2(ofRandom(50, ofGetWidth() - 50), -50);
        enemies.push_back(e);
        enemySpawnTimer = 0;
    }

    // Boss erscheint, wenn genug Gegner besiegt wurden
    if (!bossSpawned && enemiesDefeated >= enemiesToDefeatBeforeBoss)
    {
        currentBoss.active = true;
        currentBoss.pos = glm::vec2(ofGetWidth() / 2, -100);
        currentBoss.hp = 10 + currentLevel * 5;
        currentBoss.radius = 60 + currentLevel * 5;
        bossSpawned = true;
    }

    // Boss-Verhalten: langsam nach unten, feuert Schüsse, nimmt Schaden
    if (currentBoss.active) {
        currentBoss.pos.y += 50 * dt;
        if (ofRandom(1.0f) < 0.02f)
            enemyShots.push_back({ currentBoss.pos, glm::vec2(0, 300) });

        for (auto sIt = playerShots.begin(); sIt != playerShots.end(); )
        {
            if (glm::distance(sIt->pos, currentBoss.pos) < currentBoss.radius)
            {
                currentBoss.hp--;
                sIt = playerShots.erase(sIt);
            }
            else ++sIt;
        }

        if (currentBoss.hp <= 0)
        {
            score += 1000;
            bossDefeated = true;
            currentBoss.active = false;
        }
    }

    // Boss besiegt → Upgrade-Menü, nächstes Level vorbereiten
    if (bossDefeated)
    {
        inUpgradeMenu = true;
        bossDefeated = false;
        bossSpawned = false;
        enemiesDefeated = 0;
        enemies.clear();
        obstacles.clear();
        playerShots.clear();
        enemyShots.clear();
        enemiesToDefeatBeforeBoss += 5;
    }

    // Game Over prüfen + Highscore speichern
    if (player.hitpoints <= 0)
    {
        gameOver = true;
        if (score > highscore)
        {
            highscore = score;
            newHighscore = true;
            std::ofstream out("highscore.txt");
            if (out.is_open())
            {
                out << highscore;
                out.close();
            }
        }
    }

    // Hintergrund-Tiles aktualisieren (Tile-Stream)
    int tileHeight = tilePool.empty() ? 360 : tilePool[0].getHeight();

    while (tilesToDraw.size() * tileHeight - backgroundOffset < ofGetHeight() + tileHeight)
    {
        int r = ofRandom(tilePool.size());
        tilesToDraw.push_back(tilePool[r]);
    }
    while (!tilesToDraw.empty() && tileDrawOffsetIndex * tileHeight - backgroundOffset + tileHeight < 0)
    {
        tilesToDraw.pop_front();
        tileDrawOffsetIndex++;
    }
}

void ofApp::draw()
{
    // Hintergrund-Tiles zeichnen
    ofPushStyle();                   // Speichert Zeichenstil (z. B. Farbe)
    ofSetColor(255);                // Standardfarbe: Weiß (zeigt Bilder „normal“ an)
    if (!tilesToDraw.empty())
    {
        int tileHeight = tilesToDraw[0].getHeight();  // Höhe eines Tiles
        for (int i = 0; i < tilesToDraw.size(); ++i)
        {
            float y = (tileDrawOffsetIndex + i) * tileHeight - backgroundOffset;
            tilesToDraw[i].draw(0, y);  // Tile an berechneter Y-Position zeichnen
        }
    }
    ofPopStyle();  // Wiederherstellen ursprünglicher Zeichenstil

    // Wenn wir im Upgrade-Menü sind, Menü anzeigen und Rest überspringen
    if (inUpgradeMenu)
    {
        ofSetColor(255);
        ofDrawBitmapString("=== UPGRADE MENU ===", ofGetWidth() / 2 - 80, 200);
        ofDrawBitmapString("1: Faster Fire (-1000)", ofGetWidth() / 2 - 80, 240);
        ofDrawBitmapString("2: +1 Multishot (-2000)", ofGetWidth() / 2 - 80, 270);
        ofDrawBitmapString("3: +1 Max HP (-3000)", ofGetWidth() / 2 - 80, 300);
        ofDrawBitmapString("ENTER to start Level " + std::to_string(currentLevel), ofGetWidth() / 2 - 80, 340);
        ofDrawBitmapString("Score: " + std::to_string(score), ofGetWidth() / 2 - 80, 380);
        return; // Rest nicht zeichnen
    }

    // Spieler zeichnen
    player.draw();

    // UI: Lebenspunkte, Score, Level
    ofSetColor(255);
    ofDrawBitmapStringHighlight("HP: " + std::to_string(player.hitpoints), ofGetWidth() / 2 - 30, 40);
    ofDrawBitmapStringHighlight("Score: " + std::to_string(score), ofGetWidth() - 150, 40);
    ofDrawBitmapStringHighlight("Level: " + std::to_string(currentLevel), 20, 40);

    // Wenn das Spiel vorbei ist: Game-Over-Screen anzeigen
    if (gameOver)
    {
        ofDrawBitmapString("GAME OVER", ofGetWidth() / 2 - 40, ofGetHeight() / 2);
        ofDrawBitmapString("Score: " + std::to_string(score), ofGetWidth() / 2 - 40, ofGetHeight() / 2 + 20);
        ofDrawBitmapString("Highscore: " + std::to_string(highscore), ofGetWidth() / 2 - 40, ofGetHeight() / 2 + 40);
        if (newHighscore)
        {
            ofSetColor(255, 255, 0);
            ofDrawBitmapString("NEW HIGHSCORE!", ofGetWidth() / 2 - 60, ofGetHeight() / 2 + 60);
            ofSetColor(255);
            ofDrawBitmapString("Press R to restart", ofGetWidth() / 2 - 50, ofGetHeight() / 2 + 80);
        }
        return; // Rest nicht mehr zeichnen
    }

    // Spieler- und Gegnerschüsse zeichnen
    for (auto& s : playerShots)
    {
        ofSetColor(0, 255, 255);  // Cyan
        ofDrawCircle(s.pos, 5);
    }
    for (auto& s : enemyShots)
    {
        ofSetColor(255, 50, 50);  // Rot
        ofDrawCircle(s.pos, 5);
    }

    // Hindernisse zeichnen (mit Text je nach Typ)
    for (auto& obs : obstacles)
    {
        std::string label;
        switch (obs.type)
        {
        case Obstacle::UPGRADE_FIRE: ofSetColor(0, 255, 0); label = "Faster Fire"; break;
        case Obstacle::DOWNGRADE_FIRE: ofSetColor(255, 0, 0); label = "Slower Fire"; break;
        case Obstacle::UPGRADE_MULTI: ofSetColor(0, 255, 0); label = "+ Shot"; break;
        case Obstacle::DOWNGRADE_MULTI: ofSetColor(255, 0, 0); label = "- Shot"; break;
        }
        ofDrawRectangle(obs.pos.x - obs.width / 2, obs.pos.y - obs.height / 2, obs.width, obs.height);
        ofSetColor(0);
        ofDrawBitmapString(label, obs.pos.x - obs.width / 2 + 10, obs.pos.y + 10);
    }

    // Gegner zeichnen – Farbe je nach Typ, HP anzeige
    for (auto& e : enemies)
    {
        switch (e.type)
        {
        case Enemy::YELLOW: ofSetColor(255, 255, 0); break;
        case Enemy::ORANGE: ofSetColor(255, 165, 0); break;
        case Enemy::RED: ofSetColor(255, 0, 0); break;
        }
        ofDrawCircle(e.pos, e.radius);
        ofSetColor(0);
        ofDrawBitmapString(std::to_string(e.hp), e.pos.x - 4, e.pos.y + 4);
    }

    // Boss zeichnen
    if (currentBoss.active)
    {
        ofSetColor(150, 0, 255); // Lila
        ofDrawCircle(currentBoss.pos, currentBoss.radius);
        ofSetColor(255);
        ofDrawBitmapString("BOSS HP: " + std::to_string(currentBoss.hp), currentBoss.pos.x - 30, currentBoss.pos.y);
    }
}

void ofApp::restartGame()
{
    // Grundzustand zurücksetzen
    gameOver = false;
    win = false;
    inUpgradeMenu = false;
    currentLevel = 1;
    enemiesDefeated = 0;
    enemiesToDefeatBeforeBoss = 15;
    score = 0;
    newHighscore = false;
    bossSpawned = false;
    bossDefeated = false;

    // Alle Vektoren leeren
    playerShots.clear();
    enemyShots.clear();
    enemies.clear();
    obstacles.clear();

    // Spieler zurück auf permanente Werte setzen
    player.fireRate = player.baseFireRate;
    player.multiShot = player.baseMultiShot;
    maxHP = player.baseMaxHP;
    player.hitpoints = maxHP;

    // Spielerposition setzen
    player.setPosition(glm::vec2(ofGetWidth() / 2, ofGetHeight() - 50));

    // Hintergrund-Tiles neu initialisieren
    tilesToDraw.clear();
    for (int i = 0; i < 5; ++i)
    {
        int r = ofRandom(tilePool.size());
        tilesToDraw.push_back(tilePool[r]);
    }
    backgroundOffset = 0;
    tileDrawOffsetIndex = 0;

    // Timer zurücksetzen
    shootTimer = 0;
    obstacleSpawnTimer = 0;
    enemySpawnTimer = 0;
    enemyShootTimer = 0;

    // Boss-Reset
    currentBoss = Boss();
}


//--------------------------------------------------------------
// Hauptfunktion zum Starten der App
int main()
{
    ofGLFWWindowSettings settings;
    settings.setSize(740, 1280);
    settings.resizable = false;
    settings.setPosition(glm::vec2(100, 50));
    ofCreateWindow(settings);
    ofRunApp(new ofApp());
}