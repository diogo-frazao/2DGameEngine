/*
* - Game Engine Core. Responsible for main execution loop and calling 
* - Update(), HandleEvents() and Renderer() functions from GameObjects and Components.
* - m_World is an exception which Start() and Upate() are called before any other GO
* - MUST be implemented by client
************************************************************/

#include <memory>
#include "GameEngine.h"
#include "SDL.h"
#include "SDLWrapper.h"
#include "Window.h"
#include "GameWorld.h"
#include "GameObject.h"
#include "RenderComponent.h"
#include "PhysicsWorld.h"
#include "Pawn.h"
#include "Log.h"
#include "TimerManager.h"
#include "Input.h"
#include "InstanceCounter.h"
#include <iostream>


// Initialize static variables
GameWorld* GameEngine::m_World = nullptr;
float GameEngine::m_ElapsedMS = 0.f;

std::vector<GameObject*> GameEngine::m_GameObjectStack;
std::vector<RenderComponent*> GameEngine::m_RenderComponentsStack;
std::vector<Pawn*> GameEngine::m_PawnsStack;

GameEngine* GameEngine::m_Instance{ nullptr };

GameEngine::~GameEngine()
{
	delete m_Window;
	delete m_Sdl;
	delete m_Input;
	delete m_PhysicsWorld;
}	

void GameEngine::Init(const char* windowTitle, int windowWidth, int windowHeight, GameWorld* World)
{
	if (m_Instance) {
		delete this;
	}
	else {
		m_Instance = this;
		m_Sdl = new SDLWrapper(SDL_INIT_VIDEO | SDL_INIT_TIMER);
		m_Window = new Window(windowTitle, windowWidth, windowHeight, true);
		m_World = World;
		m_Input = new Input();
		m_PhysicsWorld = new PhysicsWorld();
		m_PhysicsWorld->Init();
	}
}

void GameEngine::StartAndRun()
{
	LOG("Engine start");

	Start();

	bool isRunning = true;
	SDL_Event ev;
	const int lock = 1000 / m_MaxFPS;
	Uint32 mTicksCount = SDL_GetTicks();
	while (isRunning)
	{
		while (!SDL_TICKS_PASSED(SDL_GetTicks(), mTicksCount + lock)); //Wait until ms passed
		m_ElapsedMS = (SDL_GetTicks() - mTicksCount) / 1000.0f;	


		SDL_PollEvent(&ev);
		if (ev.type == SDL_QUIT)
		{
			isRunning = false;
		}
		else
		{
			HandleInput(ev);
		}
		
		m_PhysicsWorld->UpdatePhysics();

		Update();

		Render();

		DestroyPending();
		
		//InstanceCounter::PrintCounts();

		mTicksCount = SDL_GetTicks();
	}

	/*End game cleaning (memory leaks check) */
	InstanceCounter::PrintCounts();
	for (auto obj : m_GameObjectStack) {
		AddPendingDestroy(obj);
	}
	DestroyPending();
	InstanceCounter::PrintCounts();
}

void GameEngine::DestroyPending()
{
	if (m_PendingDestroy.size() > 0) {
		for (auto obj : m_PendingDestroy) {
			std::vector<std::shared_ptr<Component>> comps = obj->GetAllComponents();
			for (auto comp : comps) {
				comp->OnDestroyed();			
				delete comp.get();
				InstanceCounter::RemoveComponentCount();
				InstanceCounter::PrintCounts();
			}
			obj->OnDestroyed();
			RemoveGameObjectFromStack(obj);
		}
		m_PendingDestroy.clear();
	}
}

void GameEngine::AddPendingDestroy(GameObject* obj)
{
	m_PendingDestroy.push_back(obj);
}

void GameEngine::Start()
{
	m_World->Init(this);
	m_World->Start();
}

void GameEngine::HandleInput(SDL_Event& ev)
{
	m_Input->ReceiveEvent(ev);

	for (int i = 0; i < m_PawnsStack.size();++i)
	{
		if (m_PawnsStack[i] != nullptr)
		{
			m_PawnsStack[i]->HandleEvents();
		}
	}
}

void GameEngine::Update()
{
	TimerManager::UpdateHandles(m_ElapsedMS);
	m_World->Update(m_ElapsedMS);
	for (int i = 0; i < m_GameObjectStack.size();++i) 
	{
		if (m_GameObjectStack[i] != nullptr)
		{
			m_GameObjectStack[i]->Update(m_ElapsedMS);
			const std::vector<std::shared_ptr<Component>> comps = m_GameObjectStack[i]->GetAllComponents();
			for (auto cpt : comps) {
				cpt->Update(m_ElapsedMS);
			}
		}
	}
}

float triangleArea(Vector2D A,Vector2D B ,Vector2D C ){
	return (C.x * B.y - B.x * C.y) - (C.x * A.y - A.x * C.y) + (B.x * A.y - A.x * B.y);
}
 bool isInsideSquare(Vector2D A ,Vector2D B ,Vector2D C ,Vector2D D ,Vector2D P)
{
	if (triangleArea(A,B,P) > 0 || triangleArea(B,C,P) > 0 || triangleArea(C,D,P) > 0 || triangleArea(D,A,P) > 0) {
		return false;
	}
	return true;
}

bool IsInside(Vector2D windowConfines, Vector2D pos, float Leeway) {

	if (pos < windowConfines) {
		return true;
	}
	//lets try for now
	return false;
}

void GameEngine::Render()
{
	m_Window->Clean();
	for (auto mR : m_RenderComponentsStack) 
	{
		Vector2D pos = mR->GetOwnerGameObject()->GetTransform()->GetPosition();
		Vector2D win = m_Window->GetWindowSize();
		if (isInsideSquare(Vector2D(-20,-20),Vector2D(win.x,-20),win,Vector2D(-20,win.y),pos)) {
			if (!mR->GetIsVisible()) {
				mR->SetIsVisible(true);
				mR->GetOwnerGameObject()->OnBecameVisible();
			}
		}
		else {
			if (mR->GetIsVisible()) {
				mR->SetIsVisible(false);
				mR->GetOwnerGameObject()->OnBecameHidden();
			}
		}
		mR->Render();
	}
	m_Window->UpdateRender();
}

void GameEngine::AddGameObjectToStack(GameObject* gameObject)
{
	if (gameObject == nullptr) { return; }
	m_GameObjectStack.push_back(gameObject);
}

void GameEngine::RemoveGameObjectFromStack(GameObject* gameObject)
{
	for (int i = 0; i < m_GameObjectStack.size(); ++i) 
	{
		if (m_GameObjectStack[i] == gameObject)
		{
			m_GameObjectStack.erase(m_GameObjectStack.begin()+i);
			InstanceCounter::RemoveObjectCount();
			return;
		}
	}
}

void GameEngine::AddRenderComponentToStack(RenderComponent* renderComponent)
{
	if (renderComponent == nullptr) { return; }
	m_RenderComponentsStack.push_back(renderComponent);
	SortRenderComponents();
}

void GameEngine::RemoveRenderComponentFromStack(RenderComponent* renderComponent)
{
	for (int i = 0; i < m_RenderComponentsStack.size(); ++i)
	{
		if (m_RenderComponentsStack[i] == renderComponent)
		{
			m_RenderComponentsStack.erase(m_RenderComponentsStack.begin() + i);
			return;
		}
	}
	SortRenderComponents();
}

void GameEngine::AddPawnToStack(Pawn* pawn)
{
	if (pawn == nullptr) { return; }
	m_PawnsStack.push_back(pawn);
}

void GameEngine::RemovePawnFromStack(Pawn* pawn)
{
	for (int i = 0; i < m_PawnsStack.size(); ++i)
	{
		if (m_PawnsStack[i] == pawn)
		{
			m_PawnsStack.erase(m_PawnsStack.begin() + i);
			return;
		}
	}
}

SDL_Renderer* GameEngine::GetRenderer() 
{
	if (m_Window) {
		return m_Window->GetRenderer();
	}
	return nullptr;
}

Vector2D GameEngine::GetWindowSize() 
{
	return m_Window->GetWindowSize();
}

void GameEngine::SortRenderComponents()
{
	LOG_WARNING("Before sort first index: " << m_RenderComponentsStack[0]);

	for (unsigned int i = 0; i < m_RenderComponentsStack.size() - 1; ++i)
	{
		int lowestIndex = i;
		for (unsigned int j = i + 1; j < m_RenderComponentsStack.size(); ++j)
		{
			if (m_RenderComponentsStack[j]->GetRenderPriority() < 
				m_RenderComponentsStack[lowestIndex]->GetRenderPriority())
			{
				lowestIndex = j;
			}
		}

		if (i != lowestIndex)
		{
			std::swap(m_RenderComponentsStack[i], m_RenderComponentsStack[lowestIndex]);
		}
	}
	LOG_WARNING("After sort first index: " << m_RenderComponentsStack[0]);
}




