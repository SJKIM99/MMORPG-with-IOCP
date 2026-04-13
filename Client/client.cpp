#define SFML_STATIC 1
#include <SFML/Graphics.hpp>
#include <SFML/Network.hpp>
#include <iostream>
#include <unordered_map>
#include <Windows.h>
#include <chrono>
#include "C:\Repository\MMORPG-with-IOCP\MMORPG-with-IOCP\GameServer\Protocol.h"

// ObjID static member definitions (required by linker)
ContentID ContentID::npos{};
ObjID ObjID::npos{};

using namespace std;

#pragma comment (lib, "opengl32.lib")
#pragma comment (lib, "winmm.lib")
#pragma comment (lib, "ws2_32.lib")

sf::TcpSocket s_socket;

constexpr auto SCREEN_WIDTH = 16;
constexpr auto SCREEN_HEIGHT = 16;

constexpr auto TILE_WIDTH = 65;
constexpr auto WINDOW_WIDTH = SCREEN_WIDTH * TILE_WIDTH;   // size of window
constexpr auto WINDOW_HEIGHT = SCREEN_WIDTH * TILE_WIDTH;

int g_left_x;
int g_top_y;
ObjID g_myid;
int chat = -1;
sf::RenderWindow* g_window;
sf::Font g_font;

class OBJECT {
private:
	bool m_showing;
	sf::Sprite m_sprite;

	sf::Text m_name;
	sf::Text m_chat;
	chrono::system_clock::time_point m_mess_end_time;
public:
	ObjID id;
	int m_x, m_y;
	int level, hp, maxhp, exp;
	char name[20];
	OBJECT(sf::Texture& t, int x, int y, int x2, int y2) {
		m_showing = false;
		m_sprite.setTexture(t);
		m_sprite.setTextureRect(sf::IntRect(x, y, x2, y2));
		set_name("NONAME");
		m_mess_end_time = chrono::system_clock::now();
	}
	OBJECT() {
		m_showing = false;
	}
	void show()
	{
		m_showing = true;
	}
	void hide()
	{
		m_showing = false;
	}

	void a_move(int x, int y) {
		m_sprite.setPosition((float)x, (float)y);
	}

	void a_draw() {
		g_window->draw(m_sprite);
	}

	void move(int x, int y) {
		m_x = x;
		m_y = y;
	}
	void draw() {
		if (false == m_showing) return;
		float rx = (m_x - g_left_x) * 65.0f + 1;
		float ry = (m_y - g_top_y) * 65.0f + 1;
		m_sprite.setPosition(rx, ry);
		g_window->draw(m_sprite);
		auto size = m_name.getGlobalBounds();
		if (m_mess_end_time < chrono::system_clock::now()) {
			m_name.setPosition(rx + 32 - size.width / 2, ry - 10);
			g_window->draw(m_name);
		}
		else {
			m_chat.setPosition(rx + 32 - size.width / 2, ry - 10);
			g_window->draw(m_chat);
		}
	}
	void set_name(const char str[]) {
		m_name.setFont(g_font);
		m_name.setString(str);
		if (id.GetCategory<EnumCategory>() == EnumCategory::eUser)
			m_name.setFillColor(sf::Color(255, 255, 255));
		else
			m_name.setFillColor(sf::Color(255, 255, 0));
		m_name.setStyle(sf::Text::Bold);
	}

	void set_chat(const char str[]) {
		m_chat.setFont(g_font);
		m_chat.setString(str);
		m_chat.setFillColor(sf::Color(255, 255, 255));
		m_chat.setStyle(sf::Text::Bold);
		m_mess_end_time = chrono::system_clock::now() + chrono::seconds(3);
	}
};

OBJECT avatar;
OBJECT monster;
OBJECT obstacle;

unordered_map <ObjID, OBJECT> players;

//OBJECT white_tile;
//OBJECT black_tile;
OBJECT tile1;
OBJECT tile2;

//sf::Texture* board;
sf::Texture* board1;
sf::Texture* board2;

sf::Texture* knight;
sf::Texture* liquid_monster;
sf::Texture* aggro_monster;

sf::Texture* wall;

void client_initialize()
{
	board1 = new sf::Texture;
	board2 = new sf::Texture;
	wall = new sf::Texture;
	knight = new sf::Texture;
	liquid_monster = new sf::Texture;
	aggro_monster = new sf::Texture;

	board1->loadFromFile("floor1.png");
	board2->loadFromFile("floor2.png");
	wall->loadFromFile("obstacle.png");
	knight->loadFromFile("knight.png");
	liquid_monster->loadFromFile("liquid_monster.png");
	aggro_monster->loadFromFile("aggro_monster.png");

	if (false == g_font.loadFromFile("cour.ttf")) {
		cout << "Font Loading Error!\n";
		exit(-1);
	}
	tile1 = OBJECT{ *board1, 0, 0, TILE_WIDTH, TILE_WIDTH };
	tile2 = OBJECT{ *board2, 0, 0, TILE_WIDTH, TILE_WIDTH };
	obstacle = OBJECT{ *wall,0,0,TILE_WIDTH,TILE_WIDTH };
	avatar = OBJECT{ *knight, 0, 0, 64, 64 };
	monster = OBJECT{ *liquid_monster, 0, 0, 64 ,64 };
	avatar.move(4, 4);
	avatar.hp = 0;
	avatar.level = 0;
}

void client_finish()
{
	players.clear();
	delete board1;
	delete board2;
	delete knight;
	delete liquid_monster;
	delete aggro_monster;
	delete wall;

	exit(0);
}

void ProcessPacket(char* ptr)
{
	static bool first_time = true;
	switch (ptr[2]) {
	case static_cast<char>(PacketType::SC_LOGIN_SUCCESS):
	{
		SC_LOGIN_SUCCESS_PACKET* packet = reinterpret_cast<SC_LOGIN_SUCCESS_PACKET*>(ptr);
		g_myid = packet->id;
		avatar.id = g_myid;
		avatar.move(packet->x, packet->y);
		g_left_x = packet->x - SCREEN_WIDTH / 2;
		g_top_y = packet->y - SCREEN_HEIGHT / 2;
		avatar.maxhp = packet->maxhp;
		avatar.hp = packet->hp;
		avatar.level = packet->level;
		avatar.exp = packet->exp;
		avatar.show();
	}
	break;

	case static_cast<char>(PacketType::SC_ADD_OBJECT):
	{
		SC_ADD_OBJECT_PACKET* my_packet = reinterpret_cast<SC_ADD_OBJECT_PACKET*>(ptr);
		ObjID id = my_packet->id;

		if (id == g_myid) {
			avatar.move(my_packet->x, my_packet->y);
			g_left_x = my_packet->x - SCREEN_WIDTH / 2;
			g_top_y = my_packet->y - SCREEN_HEIGHT / 2;
			avatar.show();
		}
		else if (id.GetCategory<EnumCategory>() == EnumCategory::eUser) {
			players[id] = OBJECT{ *knight, 0, 0, 64, 64 };
			players[id].id = id;
			players[id].move(my_packet->x, my_packet->y);
			players[id].set_name(my_packet->name);
			players[id].show();
		}
		else {
			if (my_packet->monster_type == MONSTER_TYPE::PASSIVE)
				players[id] = OBJECT{ *liquid_monster, 0, 0, 64, 64 };
			else
				players[id] = OBJECT{ *aggro_monster, 0, 0, 64, 64 };
			players[id].id = id;
			players[id].move(my_packet->x, my_packet->y);
			players[id].set_name(my_packet->name);
			players[id].show();
		}
		break;
	}
	case static_cast<char>(PacketType::SC_MOVE_OBJECT):
	{
		SC_MOVE_OBJECT_PACKET* my_packet = reinterpret_cast<SC_MOVE_OBJECT_PACKET*>(ptr);
		ObjID other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.move(my_packet->x, my_packet->y);
			g_left_x = my_packet->x - SCREEN_WIDTH / 2;
			g_top_y = my_packet->y - SCREEN_HEIGHT / 2;
		}
		else {
			players[other_id].move(my_packet->x, my_packet->y);
		}
		break;
	}

	case static_cast<char>(PacketType::SC_REMOVE_OBJECT):
	{
		SC_REMOVE_OBJECT_PACKET* my_packet = reinterpret_cast<SC_REMOVE_OBJECT_PACKET*>(ptr);
		ObjID other_id = my_packet->id;
		if (other_id == g_myid) {
			avatar.hide();
		}
		else {
			players.erase(other_id);
		}
		break;
	}
	case static_cast<char>(PacketType::SC_PLAYER_ATTACK_NPC):
	{
		// SC_PLAYER_ATTACK_NPC_PACKET* packet = reinterpret_cast<SC_PLAYER_ATTACK_NPC_PACKET*>(ptr);
		break;
	}
	case static_cast<char>(PacketType::SC_STAT_CHANGE):
	{
		SC_STAT_CHANGE_PACKET* packet = reinterpret_cast<SC_STAT_CHANGE_PACKET*>(ptr);
		avatar.level = packet->level;
		avatar.hp    = packet->hp;
		avatar.maxhp = packet->maxhp;
		avatar.exp   = packet->exp;
		break;
	}
	case static_cast<char>(PacketType::SC_NPC_DIE):
	{
		SC_NPC_DIE_PACKET* packet = reinterpret_cast<SC_NPC_DIE_PACKET*>(ptr);
		players.erase(packet->npc_id);
		break;
	}
	case static_cast<char>(PacketType::SC_NPC_RESPAWN):
	{
		SC_NPC_RESPAWN_PACKET* packet = reinterpret_cast<SC_NPC_RESPAWN_PACKET*>(ptr);
		ObjID npcId = packet->npc_id;
		if (players.count(npcId)) {
			players[npcId].move(packet->x, packet->y);
			players[npcId].show();
		}
		break;
	}
	case static_cast<char>(PacketType::SC_NPC_ATTACK_PLAYER):
	{
		SC_NPC_ATTACK_PLAYER_PACKET* packet = reinterpret_cast<SC_NPC_ATTACK_PLAYER_PACKET*>(ptr);
		avatar.hp = packet->hp;
		break;
	}
	case static_cast<char>(PacketType::SC_HEAL):
	{
		SC_HEAL_PACKET* packet = reinterpret_cast<SC_HEAL_PACKET*>(ptr);
		avatar.hp = packet->hp;
		break;
	}
	case static_cast<char>(PacketType::SC_PLAYER_DIE):
	{
		SC_PLAYER_DIE_PACKET* packet = reinterpret_cast<SC_PLAYER_DIE_PACKET*>(ptr);
		if (packet->id == g_myid) {
			avatar.hp = packet->hp;
			avatar.hide();
		}
		else {
			players[packet->id].hp = packet->hp;
			players[packet->id].hide();
		}
		break;
	}
	case static_cast<char>(PacketType::SC_PLAYER_RESPAWN):
	{
		SC_PLAYER_RESPAWN_PACKET* packet = reinterpret_cast<SC_PLAYER_RESPAWN_PACKET*>(ptr);
		if (packet->id == g_myid) {
			g_left_x = packet->x - SCREEN_WIDTH / 2;
			g_top_y = packet->y - SCREEN_HEIGHT / 2;
			avatar.move(packet->x, packet->y);
			avatar.hp = packet->hp;
			avatar.show();
		}
		else {
			players[packet->id].move(packet->x, packet->y);
			players[packet->id].hp = packet->hp;
			players[packet->id].show();
		}
		break;
	}
	/*
	case SC_LOGIN_FAIL:
		cout << "�̹� ������ �÷��̾��̹Ƿ� �α����� �� �����ϴ�. " << endl;
		client_finish();

		break;
	case SC_STAT_CHANGE: {
		SC_STAT_CHANGE_PACKET* packet = reinterpret_cast<SC_STAT_CHANGE_PACKET*>(ptr);
		avatar.maxhp = packet->max_hp;
		avatar.hp = packet->hp;
		avatar.level = packet->level;
		avatar.exp = packet->exp;
		break;
	}
	case SC_PLAYER_DIE: {
		SC_PC_DIE_PACKET* packet = reinterpret_cast<SC_PC_DIE_PACKET*>(ptr);
		int other_id = packet->id;
		players.erase(other_id);
	}
					  break;
	case SC_PC_DIE: {
		SC_PC_DIE_PACKET* packet = reinterpret_cast<SC_PC_DIE_PACKET*>(ptr);
		int my_id = packet->id;
		players.erase(my_id);

		avatar.move(packet->x, packet->y);
		g_left_x = packet->x - SCREEN_WIDTH / 2;
		g_top_y = packet->y - SCREEN_HEIGHT / 2;
		avatar.hp = packet->hp;
		avatar.exp = packet->exp;

		avatar.show();
	}
				  break;
	case SC_HEAL: {
		SC_HEAL_PACKET* packet = reinterpret_cast<SC_HEAL_PACKET*>(ptr);
		avatar.hp = packet->hp;
	}
				break;
	case SC_NPC_DIE: {
		SC_NPC_DIE_PACKET* packet = reinterpret_cast<SC_NPC_DIE_PACKET*>(ptr);
		players.erase(packet->npc_id);
	}
				   break;
	case SC_NPC_RESPAWN: {
		SC_NPC_RESPAWN_PACKET* packet = reinterpret_cast<SC_NPC_RESPAWN_PACKET*>(ptr);
		int respawn_npc_id = packet->npc_id;
		players[respawn_npc_id].move(packet->x, packet->y);
		players[respawn_npc_id].show();
	}
					   break;
	case SC_NPC_ATTACK_PLAYER: {
		SC_NPC_ATTACK_PLAYER_PACKET* packet = reinterpret_cast<SC_NPC_ATTACK_PLAYER_PACKET*>(ptr);
		avatar.hp = packet->hp;
	}
							 break;*/
	default:
		printf("Unknown PACKET type [%d]\n", ptr[2]);
	}
}

void process_data(char* net_buf, size_t io_byte)
{
	char* ptr = net_buf;
	static size_t in_packet_size = 0;
	static size_t saved_packet_size = 0;
	static char packet_buffer[BUF_SIZE];

	while (0 != io_byte) {
		if (0 == in_packet_size) {
			in_packet_size = ptr[0];
		}
		if (io_byte + saved_packet_size >= in_packet_size) {
			memcpy(packet_buffer + saved_packet_size, ptr, in_packet_size - saved_packet_size);
			ProcessPacket(packet_buffer);
			ptr += in_packet_size - saved_packet_size;
			io_byte -= in_packet_size - saved_packet_size;
			in_packet_size = 0;
			saved_packet_size = 0;
		}
		else {
			memcpy(packet_buffer + saved_packet_size, ptr, io_byte);
			saved_packet_size += io_byte;
			io_byte = 0;
		}
	}
}

void client_main()
{
	char net_buf[BUF_SIZE];
	size_t	received;

	auto recv_result = s_socket.receive(net_buf, BUF_SIZE, received);
	if (recv_result == sf::Socket::Error)
	{
		wcout << L"Recv ����!";
		exit(-1);
	}
	if (recv_result == sf::Socket::Disconnected) {
		wcout << L"Disconnected\n";
		exit(-1);
	}
	if (recv_result != sf::Socket::NotReady)
		if (received > 0) process_data(net_buf, received);
	//cout << " ������ ũ�� - " << received << endl;

	for (int i = 0; i < SCREEN_WIDTH; ++i)
		for (int j = 0; j < SCREEN_HEIGHT; ++j)
		{
			int tile_x = i + g_left_x;
			int tile_y = j + g_top_y;
			if ((tile_x < 0) || (tile_y < 0)) continue;
			if (0 == (tile_x / 3 + tile_y / 3) % 3) {
				tile1.a_move(TILE_WIDTH * i, TILE_WIDTH * j);
				tile1.a_draw();
			}
			else if (1 == (tile_x / 3 + tile_y / 3) % 3)
			{
				tile2.a_move(TILE_WIDTH * i, TILE_WIDTH * j);
				tile2.a_draw();
			}
			else {	//��ֹ� �浹 ó�� �ؾ���
				if (0 == (tile_x / 2 + tile_y / 2) % 3) {
					obstacle.a_move(TILE_WIDTH * i, TILE_WIDTH * j);
					obstacle.a_draw();
				}
				else if (1 == (tile_x / 2 + tile_y / 2) % 3) {
					tile2.a_move(TILE_WIDTH * i, TILE_WIDTH * j);
					tile2.a_draw();
				}
				else {
					tile1.a_move(TILE_WIDTH * i, TILE_WIDTH * j);
					tile1.a_draw();
				}
			}
		}
	avatar.draw();
	for (auto& pl : players) pl.second.draw();
	sf::Text text;
	text.setFont(g_font);
	char buf[512];
	sprintf_s(buf, "(%d, %d)  Lv.%d  HP:%d/%d  EXP:%d", avatar.m_x, avatar.m_y, avatar.level, avatar.hp, avatar.maxhp, avatar.exp);
	text.setString(buf);
	g_window->draw(text);
}

void send_packet(void* packet)
{
	unsigned char* p = reinterpret_cast<unsigned char*>(packet);
	size_t sent = 0;
	s_socket.send(packet, p[0], sent);
}

int main()
{
	wcout.imbue(locale("korean"));
	sf::Socket::Status status = s_socket.connect("127.0.0.1", PORT_NUM);
	s_socket.setBlocking(false);

	if (status != sf::Socket::Done) {
		wcout << L"������ ������ �� �����ϴ�.\n";
		exit(-1);
	}

	client_initialize();
	char id[20];
	cout << "ID :";
	cin >> id;
	CS_LOGIN_PACKET p;
	p.size = sizeof(p);
	p.type = static_cast<char>(PacketType::CS_LOGIN);

	strcpy_s(p.name, id);
	send_packet(&p);
	avatar.set_name(p.name);

	sf::RenderWindow window(sf::VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), "2D CLIENT");
	g_window = &window;

	while (window.isOpen())
	{
		sf::Event event;
		sf::String inputString;
		while (window.pollEvent(event))
		{
			if (chat != 1) {
				if (event.type == sf::Event::Closed)
					window.close();
				if (event.type == sf::Event::KeyPressed) {
					int direction = -1;
					int attack = -1;
					switch (event.key.code) {
					case sf::Keyboard::Left:
						direction = 2;
						break;
					case sf::Keyboard::Right:
						direction = 3;
						break;
					case sf::Keyboard::Up:
						direction = 0;
						break;
					case sf::Keyboard::Down:
						direction = 1;
						break;
					case sf::Keyboard::LControl:
						attack = 1;
						break;
					case sf::Keyboard::Enter:
						chat = 1;
						break;
					case sf::Keyboard::Escape:
						window.close();
						break;
					}
					if (-1 != direction) {
						CS_MOVE_PACKET p;
						p.size = sizeof(p);
						p.type = static_cast<char>(PacketType::CS_MOVE);
						p.direction = direction;
						p.move_time = static_cast<unsigned>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
						send_packet(&p);
					}
					if (1 == attack) {
						CS_ATTACK_PACKET p;
						p.size = sizeof(p);
						p.type = static_cast<char>(PacketType::CS_ATTACK);
						p.attack_time = static_cast<unsigned>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
						send_packet(&p);
					}

				}
			}
			else {
			/*	char chatting[CHAT_SIZE];
				cout << "ä���Է� : ";
				cin >> chatting;

				CS_CHAT_PACKET p;
				p.size = sizeof(p);
				p.type = CS_CHAT;
				strcpy_s(p.mess, chatting);
				chat = -1;
				send_packet(&p);*/
			}
		}

		window.clear();
		client_main();
		window.display();
	}
	client_finish();

	return 0;
}