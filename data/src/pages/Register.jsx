import { useState } from "react";
import { useNavigate } from "react-router-dom";

export default function Register() {
  const navigate = useNavigate();
  const [user, setUser] = useState({ username: "", password: "" });

  const handleSubmit = e => {
    e.preventDefault();
    // salvar no ESP32?
    navigate("/login");
  };

  return (
    <main className="center">
      <h2>Registrar</h2>
      <form onSubmit={handleSubmit}>
        <input
          placeholder="Usuário"
          value={user.username}
          onChange={e => setUser({ ...user, username: e.target.value })}
        />
        <input
          placeholder="Senha"
          type="password"
          value={user.password}
          onChange={e => setUser({ ...user, password: e.target.value })}
        />
        <button type="submit">Criar conta</button>
      </form>
    </main>
  );
}
