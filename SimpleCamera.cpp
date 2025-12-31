//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

//#include "stdafx.h"
#include "SimpleCamera.h"
#include <cmath>

SimpleCamera::SimpleCamera() :
	m_initialPosition(0, 0, 0),
	m_position(m_initialPosition),
	m_yaw(XM_PI),
	m_pitch(0.0f),
	//m_lookDirection(0, 0, -1),
	//m_upDirection(0, 1, 0),
	m_moveSpeed(20.0f),
	m_turnSpeed(XM_PIDIV2),
	m_keysPressed{}
{
}

void SimpleCamera::Init(XMFLOAT3 position)
{
	m_initialPosition = position;
	Reset();
}

void SimpleCamera::SetMoveSpeed(float unitsPerSecond)
{
	m_moveSpeed = unitsPerSecond;
}

void SimpleCamera::SetTurnSpeed(float radiansPerSecond)
{
	m_turnSpeed = radiansPerSecond;
}

void SimpleCamera::Reset()
{
	m_position = m_initialPosition;
	//m_yaw = XM_PI;
	m_yaw = 0.0f;
	m_pitch = 0.0f;
	//m_lookDirection = { 0, 0, -1 };
}

void SimpleCamera::Update(float elapsedSeconds)
{
	// 入力から回転（矢印キー）を先に更新
	float rotateInterval = m_turnSpeed * elapsedSeconds;
	if (m_keysPressed.left)
		m_yaw -= rotateInterval;
	if (m_keysPressed.right)
		m_yaw += rotateInterval;
	if (m_keysPressed.up)
		m_pitch -= rotateInterval;
	if (m_keysPressed.down)
		m_pitch += rotateInterval;

	// カメラ基準の移動入力（WSAD: 前後左右、QE: 上下）
	XMFLOAT3 moveInput(0, 0, 0);
	if (m_keysPressed.a) moveInput.x -= 1.0f; // 左
	if (m_keysPressed.d) moveInput.x += 1.0f; // 右
	if (m_keysPressed.w) moveInput.z += 1.0f; // 前
	if (m_keysPressed.s) moveInput.z -= 1.0f; // 後
	if (m_keysPressed.q) moveInput.y += 1.0f; // 上
	if (m_keysPressed.e) moveInput.y -= 1.0f; // 下

	// 入力ベクトルを正規化（斜め移動時に速度が速くならないように）
	XMVECTOR moveVec = XMLoadFloat3(&moveInput);
	float length = XMVectorGetX(XMVector3Length(moveVec));
	if (length > 1.0f)
	{
		moveVec = XMVector3Normalize(moveVec);
		XMStoreFloat3(&moveInput, moveVec);
	}

	if (length > 0)
	{
		XMVECTOR positionVec = XMLoadFloat3(&m_position);
		XMVECTOR moveVecNorm = moveVec;

		// 移動ベクトルを回転
		XMVECTOR rotatedMove = XMVector3Rotate(moveVecNorm, XMQuaternionRotationRollPitchYaw(m_pitch, m_yaw, 0));
		// 移動速度と経過時間を掛ける
		float moveInterval = m_moveSpeed * elapsedSeconds;
		rotatedMove = XMVectorScale(rotatedMove, moveInterval);
		// 位置を更新
		positionVec = XMVectorAdd(positionVec, rotatedMove);
		XMStoreFloat3(&m_position, positionVec);
	}

}

XMMATRIX SimpleCamera::GetViewMatrix()
{
	// return XMMatrixLookToRH(XMLoadFloat3(&m_position), XMLoadFloat3(&m_lookDirection), XMLoadFloat3(&m_upDirection));
	return XMMatrixInverse(nullptr,
		XMMatrixRotationRollPitchYaw(m_pitch, m_yaw, 0) *
		XMMatrixTranslationFromVector(XMLoadFloat3(&m_position))
	);
}

XMMATRIX SimpleCamera::GetProjectionMatrix(float fov, float aspectRatio, float nearPlane, float farPlane)
{
	return XMMatrixPerspectiveFovLH(fov, aspectRatio, nearPlane, farPlane);
}

void SimpleCamera::OnKeyDown(WPARAM key)
{
	switch (key)
	{
	case 'W':
		m_keysPressed.w = true;
		break;
	case 'A':
		m_keysPressed.a = true;
		break;
	case 'S':
		m_keysPressed.s = true;
		break;
	case 'D':
		m_keysPressed.d = true;
		break;
	case 'Q':
		m_keysPressed.q = true;
		break;
	case 'E':
		m_keysPressed.e = true;
		break;
	case VK_LEFT:
		m_keysPressed.left = true;
		break;
	case VK_RIGHT:
		m_keysPressed.right = true;
		break;
	case VK_UP:
		m_keysPressed.up = true;
		break;
	case VK_DOWN:
		m_keysPressed.down = true;
		break;
	case VK_ESCAPE:
		Reset();
		break;
	}
}

void SimpleCamera::OnKeyUp(WPARAM key)
{
	switch (key)
	{
	case 'W':
		m_keysPressed.w = false;
		break;
	case 'A':
		m_keysPressed.a = false;
		break;
	case 'S':
		m_keysPressed.s = false;
		break;
	case 'D':
		m_keysPressed.d = false;
		break;
	case 'Q':
		m_keysPressed.q = false;
		break;
	case 'E':
		m_keysPressed.e = false;
		break;
	case VK_LEFT:
		m_keysPressed.left = false;
		break;
	case VK_RIGHT:
		m_keysPressed.right = false;
		break;
	case VK_UP:
		m_keysPressed.up = false;
		break;
	case VK_DOWN:
		m_keysPressed.down = false;
		break;
	}
}
